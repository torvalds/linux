/*******************************************************************************
*
*   (c) 1999 by Computone Corporation
*
********************************************************************************
*
*
*   PACKAGE:     Linux tty Device Driver for IntelliPort II family of multiport
*                serial I/O controllers.
*
*   DESCRIPTION: Defines, definitions and includes which are heavily dependent
*                on O/S, host, compiler, etc. This file is tailored for:
*                 Linux v2.0.0 and later
*                 Gnu gcc c2.7.2
*                 80x86 architecture
*
*******************************************************************************/

#ifndef I2OS_H    /* To prevent multiple includes */
#define I2OS_H 1

//-------------------------------------------------
// Required Includes
//-------------------------------------------------

#include "ip2types.h"
#include <asm/io.h>  /* For inb, etc */

//------------------------------------
// Defines for I/O instructions:
//------------------------------------

#define INB(port)                inb(port)
#define OUTB(port,value)         outb((value),(port))
#define INW(port)                inw(port)
#define OUTW(port,value)         outw((value),(port))
#define OUTSW(port,addr,count)   outsw((port),(addr),(((count)+1)/2))
#define OUTSB(port,addr,count)   outsb((port),(addr),(((count)+1))&-2)
#define INSW(port,addr,count)    insw((port),(addr),(((count)+1)/2))
#define INSB(port,addr,count)    insb((port),(addr),(((count)+1))&-2)

//--------------------------------------------
// Interrupt control
//--------------------------------------------

#define LOCK_INIT(a)	rwlock_init(a)

#define SAVE_AND_DISABLE_INTS(a,b) { \
	/* printk("get_lock: 0x%x,%4d,%s\n",(int)a,__LINE__,__FILE__);*/ \
	spin_lock_irqsave(a,b); \
}

#define RESTORE_INTS(a,b) { \
	/* printk("rel_lock: 0x%x,%4d,%s\n",(int)a,__LINE__,__FILE__);*/ \
	spin_unlock_irqrestore(a,b); \
}

#define READ_LOCK_IRQSAVE(a,b) { \
	/* printk("get_read_lock: 0x%x,%4d,%s\n",(int)a,__LINE__,__FILE__);*/ \
	read_lock_irqsave(a,b); \
}

#define READ_UNLOCK_IRQRESTORE(a,b) { \
	/* printk("rel_read_lock: 0x%x,%4d,%s\n",(int)a,__LINE__,__FILE__);*/ \
	read_unlock_irqrestore(a,b); \
}

#define WRITE_LOCK_IRQSAVE(a,b) { \
	/* printk("get_write_lock: 0x%x,%4d,%s\n",(int)a,__LINE__,__FILE__);*/ \
	write_lock_irqsave(a,b); \
}

#define WRITE_UNLOCK_IRQRESTORE(a,b) { \
	/* printk("rel_write_lock: 0x%x,%4d,%s\n",(int)a,__LINE__,__FILE__);*/ \
	write_unlock_irqrestore(a,b); \
}


//------------------------------------------------------------------------------
// Hardware-delay loop
//
// Probably used in only one place (see i2ellis.c) but this helps keep things
// together. Note we have unwound the IN instructions. On machines with a
// reasonable cache, the eight instructions (1 byte each) should fit in cache
// nicely, and on un-cached machines, the code-fetch would tend not to dominate.
// Note that cx is shifted so that "count" still reflects the total number of
// iterations assuming no unwinding.
//------------------------------------------------------------------------------

//#define  DELAY1MS(port,count,label)

//------------------------------------------------------------------------------
// Macros to switch to a new stack, saving stack pointers, and to restore the
// old stack (Used, for example, in i2lib.c) "heap" is the address of some
// buffer which will become the new stack (working down from highest address).
// The two words at the two lowest addresses in this stack are for storing the
// SS and SP.
//------------------------------------------------------------------------------

//#define  TO_NEW_STACK(heap,size)
//#define  TO_OLD_STACK(heap)

//------------------------------------------------------------------------------
// Macros to save the original IRQ vectors and masks, and to patch in new ones.
//------------------------------------------------------------------------------

//#define  SAVE_IRQ_MASKS(dest)
//#define  WRITE_IRQ_MASKS(src)
//#define  SAVE_IRQ_VECTOR(value,dest)
//#define  WRITE_IRQ_VECTOR(value,src)

//------------------------------------------------------------------------------
// Macro to copy data from one far pointer to another.
//------------------------------------------------------------------------------

#define  I2_MOVE_DATA(fpSource,fpDest,count) memmove(fpDest,fpSource,count);

//------------------------------------------------------------------------------
// Macros to issue eoi's to host interrupt control (IBM AT 8259-style).
//------------------------------------------------------------------------------

//#define MASTER_EOI
//#define SLAVE_EOI

#endif   /* I2OS_H */


