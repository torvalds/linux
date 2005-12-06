/*
 * ipmi_si_sm.h
 *
 * State machine interface for low-level IPMI system management
 * interface state machines.  This code is the interface between
 * the ipmi_smi code (that handles the policy of a KCS, SMIC, or
 * BT interface) and the actual low-level state machine.
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* This is defined by the state machines themselves, it is an opaque
   data type for them to use. */
struct si_sm_data;

/* The structure for doing I/O in the state machine.  The state
   machine doesn't have the actual I/O routines, they are done through
   this interface. */
struct si_sm_io
{
	unsigned char (*inputb)(struct si_sm_io *io, unsigned int offset);
	void (*outputb)(struct si_sm_io *io,
			unsigned int  offset,
			unsigned char b);

	/* Generic info used by the actual handling routines, the
           state machine shouldn't touch these. */
	void *info;
	void __iomem *addr;
	int  regspacing;
	int  regsize;
	int  regshift;
};

/* Results of SMI events. */
enum si_sm_result
{
	SI_SM_CALL_WITHOUT_DELAY, /* Call the driver again immediately */
	SI_SM_CALL_WITH_DELAY,	/* Delay some before calling again. */
	SI_SM_CALL_WITH_TICK_DELAY,	/* Delay at least 1 tick before calling again. */
	SI_SM_TRANSACTION_COMPLETE, /* A transaction is finished. */
	SI_SM_IDLE,		/* The SM is in idle state. */
	SI_SM_HOSED,		/* The hardware violated the state machine. */
	SI_SM_ATTN		/* The hardware is asserting attn and the
				   state machine is idle. */
};

/* Handlers for the SMI state machine. */
struct si_sm_handlers
{
	/* Put the version number of the state machine here so the
           upper layer can print it. */
	char *version;

	/* Initialize the data and return the amount of I/O space to
           reserve for the space. */
	unsigned int (*init_data)(struct si_sm_data *smi,
				  struct si_sm_io   *io);

	/* Start a new transaction in the state machine.  This will
	   return -2 if the state machine is not idle, -1 if the size
	   is invalid (to large or too small), or 0 if the transaction
	   is successfully completed. */
	int (*start_transaction)(struct si_sm_data *smi,
				 unsigned char *data, unsigned int size);

	/* Return the results after the transaction.  This will return
	   -1 if the buffer is too small, zero if no transaction is
	   present, or the actual length of the result data. */
	int (*get_result)(struct si_sm_data *smi,
			  unsigned char *data, unsigned int length);

	/* Call this periodically (for a polled interface) or upon
	   receiving an interrupt (for a interrupt-driven interface).
	   If interrupt driven, you should probably poll this
	   periodically when not in idle state.  This should be called
	   with the time that passed since the last call, if it is
	   significant.  Time is in microseconds. */
	enum si_sm_result (*event)(struct si_sm_data *smi, long time);

	/* Attempt to detect an SMI.  Returns 0 on success or nonzero
           on failure. */
	int (*detect)(struct si_sm_data *smi);

	/* The interface is shutting down, so clean it up. */
	void (*cleanup)(struct si_sm_data *smi);

	/* Return the size of the SMI structure in bytes. */
	int (*size)(void);
};

/* Current state machines that we can use. */
extern struct si_sm_handlers kcs_smi_handlers;
extern struct si_sm_handlers smic_smi_handlers;
extern struct si_sm_handlers bt_smi_handlers;

