/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2016 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2016 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#include "gc_hal_kernel_precomp.h"

#if gcdENABLE_VG

/******************************************************************************\
*********************** Support Functions and Definitions **********************
\******************************************************************************/

/* Interruot statistics will be accumulated if not zero. */
#define gcmENABLE_INTERRUPT_STATISTICS 0

#define _GC_OBJ_ZONE    gcvZONE_INTERRUPT

/* Object structure. */
struct _gckVGINTERRUPT
{
    /* Object. */
    gcsOBJECT                   object;

    /* gckVGKERNEL pointer. */
    gckVGKERNEL                 kernel;

    /* gckOS pointer. */
    gckOS                       os;

    /* Interrupt handlers. */
    gctINTERRUPT_HANDLER        handlers[32];

    /* Main interrupt handler thread. */
    gctTHREAD                   handler;
    gctBOOL                     terminate;

    /* Interrupt FIFO. */
    gctSEMAPHORE                fifoValid;
    gctUINT32                   fifo[256];
    gctUINT                     fifoItems;
    gctUINT8                    head;
    gctUINT8                    tail;

    /* Interrupt statistics. */
#if gcmENABLE_INTERRUPT_STATISTICS
    gctUINT                     maxFifoItems;
    gctUINT                     fifoOverflow;
    gctUINT                     maxSimultaneous;
    gctUINT                     multipleCount;
#endif
};


/*******************************************************************************
**
**  _ProcessInterrupt
**
**  The interrupt processor.
**
**  INPUT:
**
**      ThreadParameter
**          Pointer to the gckVGINTERRUPT object.
**
**  OUTPUT:
**
**      Nothing.
*/

#if gcmENABLE_INTERRUPT_STATISTICS
static void
_ProcessInterrupt(
    gckVGINTERRUPT Interrupt,
    gctUINT_PTR TriggeredCount
    )
#else
static void
_ProcessInterrupt(
    gckVGINTERRUPT Interrupt
    )
#endif
{
    gceSTATUS status;
    gctUINT32 triggered;
    gctUINT i;

    /* Advance to the next entry. */
    Interrupt->tail      += 1;
    Interrupt->fifoItems -= 1;

    /* Get the interrupt value. */
    triggered = Interrupt->fifo[Interrupt->tail];
    gcmkASSERT(triggered != 0);

    gcmkTRACE_ZONE(
        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
        "%s: triggered=0x%08X\n",
        __FUNCTION__,
        triggered
        );

    /* Walk through all possible interrupts. */
    for (i = 0; i < gcmSIZEOF(Interrupt->handlers); i += 1)
    {
        /* Test if interrupt happened. */
        if ((triggered & 1) == 1)
        {
#if gcmENABLE_INTERRUPT_STATISTICS
            if (TriggeredCount != gcvNULL)
            {
                (* TriggeredCount) += 1;
            }
#endif

            /* Make sure we have valid handler. */
            if (Interrupt->handlers[i] == gcvNULL)
            {
                gcmkTRACE(
                    gcvLEVEL_ERROR,
                    "%s: Interrupt %d isn't registered.\n",
                    __FUNCTION__, i
                    );
            }
            else
            {
                gcmkTRACE_ZONE(
                    gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
                    "%s: interrupt=%d\n",
                    __FUNCTION__,
                    i
                    );

                /* Call the handler. */
                status = Interrupt->handlers[i] (Interrupt->kernel);

                if (gcmkIS_ERROR(status))
                {
                    /* Failed to signal the semaphore. */
                    gcmkTRACE(
                        gcvLEVEL_ERROR,
                        "%s: Error %d incrementing the semaphore #%d.\n",
                        __FUNCTION__, status, i
                        );
                }
            }
        }

        /* Next interrupt. */
        triggered >>= 1;

        /* No more interrupts to handle? */
        if (triggered == 0)
        {
            break;
        }
    }
}


/*******************************************************************************
**
**  _MainInterruptHandler
**
**  The main interrupt thread serves the interrupt FIFO and calls registered
**  handlers for the interrupts that occured. The handlers are called in the
**  sequence interrupts occured with the exception when multiple interrupts
**  occured at the same time. In that case the handler calls are "sorted" by
**  the interrupt number therefore giving the interrupts with lower numbers
**  higher priority.
**
**  INPUT:
**
**      ThreadParameter
**          Pointer to the gckVGINTERRUPT object.
**
**  OUTPUT:
**
**      Nothing.
*/

static gctTHREADFUNCRESULT gctTHREADFUNCTYPE
_MainInterruptHandler(
    gctTHREADFUNCPARAMETER ThreadParameter
    )
{
    gceSTATUS status;
    gckVGINTERRUPT interrupt;

#if gcmENABLE_INTERRUPT_STATISTICS
    gctUINT count;
#endif

    /* Cast the object. */
    interrupt = (gckVGINTERRUPT) ThreadParameter;

    /* Enter the loop. */
    while (gcvTRUE)
    {
        /* Wait for an interrupt. */
        status = gckOS_DecrementSemaphore(interrupt->os, interrupt->fifoValid);

        /* Error? */
        if (gcmkIS_ERROR(status))
        {
            break;
        }

        /* System termination request? */
        if (status == gcvSTATUS_TERMINATE)
        {
            break;
        }

        /* Driver is shutting down? */
        if (interrupt->terminate)
        {
            break;
        }

#if gcmENABLE_INTERRUPT_STATISTICS
        /* Reset triggered count. */
        count = 0;

        /* Process the interrupt. */
        _ProcessInterrupt(interrupt, &count);

        /* Update conters. */
        if (count > interrupt->maxSimultaneous)
        {
            interrupt->maxSimultaneous = count;
        }

        if (count > 1)
        {
            interrupt->multipleCount += 1;
        }
#else
        /* Process the interrupt. */
        _ProcessInterrupt(interrupt);
#endif
    }

    return 0;
}


/*******************************************************************************
**
**  _StartInterruptHandler / _StopInterruptHandler
**
**  Main interrupt handler routine control.
**
**  INPUT:
**
**      ThreadParameter
**          Pointer to the gckVGINTERRUPT object.
**
**  OUTPUT:
**
**      Nothing.
*/

static gceSTATUS
_StartInterruptHandler(
    gckVGINTERRUPT Interrupt
    )
{
    gceSTATUS status, last;

    do
    {
        /* Objects must not be already created. */
        gcmkASSERT(Interrupt->fifoValid == gcvNULL);
        gcmkASSERT(Interrupt->handler   == gcvNULL);

        /* Reset the termination request. */
        Interrupt->terminate = gcvFALSE;

#if !gcdENABLE_INFINITE_SPEED_HW
        /* Construct the fifo semaphore. */
        gcmkERR_BREAK(gckOS_CreateSemaphoreVG(
            Interrupt->os, &Interrupt->fifoValid
            ));

        /* Start the interrupt handler thread. */
        gcmkERR_BREAK(gckOS_StartThread(
            Interrupt->os,
            _MainInterruptHandler,
            Interrupt,
            &Interrupt->handler
            ));
#endif

        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Roll back. */
    if (Interrupt->fifoValid != gcvNULL)
    {
        gcmkCHECK_STATUS(gckOS_DestroySemaphore(
            Interrupt->os, Interrupt->fifoValid
            ));

        Interrupt->fifoValid = gcvNULL;
    }

    /* Return the status. */
    return status;
}

static gceSTATUS
_StopInterruptHandler(
    gckVGINTERRUPT Interrupt
    )
{
    gceSTATUS status;

    do
    {
        /* Does the thread exist? */
        if (Interrupt->handler == gcvNULL)
        {
            /* The semaphore must be NULL as well. */
            gcmkASSERT(Interrupt->fifoValid == gcvNULL);

            /* Success. */
            status = gcvSTATUS_OK;
            break;
        }

        /* The semaphore must exist as well. */
        gcmkASSERT(Interrupt->fifoValid != gcvNULL);

        /* Set the termination request. */
        Interrupt->terminate = gcvTRUE;

        /* Unlock the thread. */
        gcmkERR_BREAK(gckOS_IncrementSemaphore(
            Interrupt->os, Interrupt->fifoValid
            ));

        /* Wait until the thread quits. */
        gcmkERR_BREAK(gckOS_StopThread(
            Interrupt->os,
            Interrupt->handler
            ));

        /* Destroy the semaphore. */
        gcmkERR_BREAK(gckOS_DestroySemaphore(
            Interrupt->os, Interrupt->fifoValid
            ));

        /* Reset handles. */
        Interrupt->handler   = gcvNULL;
        Interrupt->fifoValid = gcvNULL;
    }
    while (gcvFALSE);

    /* Return the status. */
    return status;
}


/******************************************************************************\
***************************** Interrupt Object API *****************************
\******************************************************************************/

/*******************************************************************************
**
**  gckVGINTERRUPT_Construct
**
**  Construct an interrupt object.
**
**  INPUT:
**
**      Kernel
**          Pointer to the gckVGKERNEL object.
**
**  OUTPUT:
**
**      Interrupt
**          Pointer to the new gckVGINTERRUPT object.
*/

gceSTATUS
gckVGINTERRUPT_Construct(
    IN gckVGKERNEL Kernel,
    OUT gckVGINTERRUPT * Interrupt
    )
{
    gceSTATUS status;
    gckVGINTERRUPT interrupt = gcvNULL;

    gcmkHEADER_ARG("Kernel=0x%x Interrupt=0x%x", Kernel, Interrupt);

    /* Verify argeuments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Interrupt != gcvNULL);

    do
    {
        /* Allocate the gckVGINTERRUPT structure. */
        gcmkERR_BREAK(gckOS_Allocate(
            Kernel->os,
            gcmSIZEOF(struct _gckVGINTERRUPT),
            (gctPOINTER *) &interrupt
            ));

        /* Reset the object data. */
        gcmkVERIFY_OK(gckOS_ZeroMemory(
            interrupt, gcmSIZEOF(struct _gckVGINTERRUPT)
            ));

        /* Initialize the object. */
        interrupt->object.type = gcvOBJ_INTERRUPT;

        /* Initialize the object pointers. */
        interrupt->kernel = Kernel;
        interrupt->os     = Kernel->os;

        /* Initialize the current FIFO position. */
        interrupt->head = (gctUINT8)~0;
        interrupt->tail = (gctUINT8)~0;

        /* Start the thread. */
        gcmkERR_BREAK(_StartInterruptHandler(interrupt));

        /* Return interrupt object. */
        *Interrupt = interrupt;

        gcmkFOOTER_ARG("*Interrup=0x%x", *Interrupt);
        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Roll back. */
    if (interrupt != gcvNULL)
    {
        /* Free the gckVGINTERRUPT structure. */
        gcmkVERIFY_OK(gckOS_Free(interrupt->os, interrupt));
    }

    gcmkFOOTER();

    /* Return the status. */
    return status;
}


/*******************************************************************************
**
**  gckVGINTERRUPT_Destroy
**
**  Destroy an interrupt object.
**
**  INPUT:
**
**      Interrupt
**          Pointer to the gckVGINTERRUPT object to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/

gceSTATUS
gckVGINTERRUPT_Destroy(
    IN gckVGINTERRUPT Interrupt
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Interrupt=0x%x", Interrupt);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Interrupt, gcvOBJ_INTERRUPT);

    do
    {
        /* Stop the interrupt thread. */
        gcmkERR_BREAK(_StopInterruptHandler(Interrupt));

        /* Mark the object as unknown. */
        Interrupt->object.type = gcvOBJ_UNKNOWN;

        /* Free the gckVGINTERRUPT structure. */
        gcmkERR_BREAK(gckOS_Free(Interrupt->os, Interrupt));
    }
    while (gcvFALSE);

    gcmkFOOTER();

    /* Return the status. */
    return status;
}


/*******************************************************************************
**
**  gckVGINTERRUPT_DumpState
**
**  Print the current state of the interrupt manager.
**
**  INPUT:
**
**      Interrupt
**          Pointer to a gckVGINTERRUPT object.
**
**  OUTPUT:
**
**      Nothing.
*/

#if gcvDEBUG
gceSTATUS
gckVGINTERRUPT_DumpState(
    IN gckVGINTERRUPT Interrupt
    )
{
    gcmkHEADER_ARG("Interrupt=0x%x", Interrupt);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Interrupt, gcvOBJ_INTERRUPT);

    /* Print the header. */
    gcmkTRACE_ZONE(
        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
        "%s: INTERRUPT OBJECT STATUS\n",
        __FUNCTION__
        );

    /* Print statistics. */
#if gcmENABLE_INTERRUPT_STATISTICS
    gcmkTRACE_ZONE(
        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
        "  Maximum number of FIFO items accumulated at a single time: %d\n",
        Interrupt->maxFifoItems
        );

    gcmkTRACE_ZONE(
        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
        "  Interrupt FIFO overflow happened times: %d\n",
        Interrupt->fifoOverflow
        );

    gcmkTRACE_ZONE(
        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
        "  Maximum number of interrupts simultaneously generated: %d\n",
        Interrupt->maxSimultaneous
        );

    gcmkTRACE_ZONE(
        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
        "  Number of times when there were multiple interrupts generated: %d\n",
        Interrupt->multipleCount
        );
#endif

    gcmkTRACE_ZONE(
        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
        "  The current number of entries in the FIFO: %d\n",
        Interrupt->fifoItems
        );

    /* Print the FIFO contents. */
    if (Interrupt->fifoItems != 0)
    {
        gctUINT8 index;
        gctUINT8 last;

        gcmkTRACE_ZONE(
            gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
            "  FIFO current contents:\n"
            );

        /* Get the current pointers. */
        index = Interrupt->tail;
        last  = Interrupt->head;

        while (index != last)
        {
            /* Advance to the next entry. */
            index += 1;

            gcmkTRACE_ZONE(
                gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
                "    %d: 0x%08X\n",
                index, Interrupt->fifo[index]
                );
        }
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}
#endif


/*******************************************************************************
**
**  gckVGINTERRUPT_Enable
**
**  Enable the specified interrupt.
**
**  INPUT:
**
**      Interrupt
**          Pointer to a gckVGINTERRUPT object.
**
**      Id
**          Pointer to the variable that holds the interrupt number to be
**          registered in range 0..31.
**          If the value is less then 0, gckVGINTERRUPT_Enable will attempt
**          to find an unused interrupt. If such interrupt is found, the number
**          will be assigned to the variable if the functuion call succeedes.
**
**      Handler
**          Pointer to the handler to register for the interrupt.
**
**  OUTPUT:
**
**      Nothing.
*/

gceSTATUS
gckVGINTERRUPT_Enable(
    IN gckVGINTERRUPT Interrupt,
    IN OUT gctINT32_PTR Id,
    IN gctINTERRUPT_HANDLER Handler
    )
{
    gceSTATUS status;
    gctINT32 i;

    gcmkHEADER_ARG("Interrupt=0x%x Id=0x%x Handler=0x%x", Interrupt, Id, Handler);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Interrupt, gcvOBJ_INTERRUPT);
    gcmkVERIFY_ARGUMENT(Id != gcvNULL);
    gcmkVERIFY_ARGUMENT(Handler != gcvNULL);

    do
    {
        /* See if we need to allocate an ID. */
        if (*Id < 0)
        {
            /* Find the first unused interrupt handler. */
            for (i = 0; i < gcmCOUNTOF(Interrupt->handlers); ++i)
            {
                if (Interrupt->handlers[i] == gcvNULL)
                {
                    break;
                }
            }

            /* No unused innterrupts? */
            if (i == gcmCOUNTOF(Interrupt->handlers))
            {
                status = gcvSTATUS_OUT_OF_RESOURCES;
                break;
            }

            /* Update the interrupt ID. */
            *Id = i;
        }

        /* Make sure the ID is in range. */
        else if (*Id >= gcmCOUNTOF(Interrupt->handlers))
        {
            status = gcvSTATUS_INVALID_ARGUMENT;
            break;
        }

        /* Set interrupt handler. */
        Interrupt->handlers[*Id] = Handler;

        /* Success. */
        status = gcvSTATUS_OK;
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return the status. */
    return status;
}


/*******************************************************************************
**
**  gckVGINTERRUPT_Disable
**
**  Disable the specified interrupt.
**
**  INPUT:
**
**      Interrupt
**          Pointer to a gckVGINTERRUPT object.
**
**      Id
**          Interrupt number to be disabled in range 0..31.
**
**  OUTPUT:
**
**      Nothing.
*/

gceSTATUS
gckVGINTERRUPT_Disable(
    IN gckVGINTERRUPT Interrupt,
    IN gctINT32 Id
    )
{
    gcmkHEADER_ARG("Interrupt=0x%x Id=0x%x", Interrupt, Id);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Interrupt, gcvOBJ_INTERRUPT);
    gcmkVERIFY_ARGUMENT((Id >= 0) && (Id < gcmCOUNTOF(Interrupt->handlers)));

    /* Reset interrupt handler. */
    Interrupt->handlers[Id] = gcvNULL;

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}


/*******************************************************************************
**
**  gckVGINTERRUPT_Enque
**
**  Read the interrupt status register and put the value in the interrupt FIFO.
**
**  INPUT:
**
**      Interrupt
**          Pointer to a gckVGINTERRUPT object.
**
**  OUTPUT:
**
**      Nothing.
*/

#ifndef __QNXNTO__
gceSTATUS
gckVGINTERRUPT_Enque(
    IN gckVGINTERRUPT Interrupt
    )
#else
gceSTATUS
gckVGINTERRUPT_Enque(
    IN gckVGINTERRUPT Interrupt,
    OUT gckOS *Os,
    OUT gctSEMAPHORE *Semaphore
    )
#endif
{
    gceSTATUS status;
    gctUINT32 triggered;

    gcmkHEADER_ARG("Interrupt=0x%x", Interrupt);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Interrupt, gcvOBJ_INTERRUPT);

#ifdef __QNXNTO__
    *Os = gcvNULL;
    *Semaphore = gcvNULL;
#endif

    do
    {
        /* Read interrupt status register. */
        gcmkERR_BREAK(gckVGHARDWARE_ReadInterrupt(
            Interrupt->kernel->hardware, &triggered
            ));

        /* Mask out TS overflow interrupt */
        triggered &= 0xfffffffe;

        /* No interrupts to process? */
        if (triggered == 0)
        {
            status = gcvSTATUS_NOT_OUR_INTERRUPT;
            break;
        }

        /* FIFO overflow? */
        if (Interrupt->fifoItems == gcmCOUNTOF(Interrupt->fifo))
        {
#if gcmENABLE_INTERRUPT_STATISTICS
            Interrupt->fifoOverflow += 1;
#endif

            /* OR the interrupt with the last value in the FIFO. */
            Interrupt->fifo[Interrupt->head] |= triggered;

            /* Success (kind of). */
            status = gcvSTATUS_OK;
        }
        else
        {
            /* Advance to the next entry. */
            Interrupt->head      += 1;
            Interrupt->fifoItems += 1;

#if gcmENABLE_INTERRUPT_STATISTICS
            if (Interrupt->fifoItems > Interrupt->maxFifoItems)
            {
                Interrupt->maxFifoItems = Interrupt->fifoItems;
            }
#endif

            /* Set the new value. */
            Interrupt->fifo[Interrupt->head] = triggered;

#ifndef __QNXNTO__
            /* Increment the FIFO semaphore. */
            gcmkERR_BREAK(gckOS_IncrementSemaphore(
                Interrupt->os, Interrupt->fifoValid
                ));
#else
            *Os = Interrupt->os;
            *Semaphore = Interrupt->fifoValid;
#endif

            /* Windows kills our threads prematurely when the application
               exists. Verify here that the thread is still alive. */
            status = gckOS_VerifyThread(Interrupt->os, Interrupt->handler);

            /* Has the thread been prematurely terminated? */
            if (status != gcvSTATUS_OK)
            {
                /* Process all accumulated interrupts. */
                while (Interrupt->head != Interrupt->tail)
                {
#if gcmENABLE_INTERRUPT_STATISTICS
                    /* Process the interrupt. */
                    _ProcessInterrupt(Interrupt, gcvNULL);
#else
                    /* Process the interrupt. */
                    _ProcessInterrupt(Interrupt);
#endif
                }

                /* Set success. */
                status = gcvSTATUS_OK;
            }
        }
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return status. */
    return status;
}

#endif /* gcdENABLE_VG */
