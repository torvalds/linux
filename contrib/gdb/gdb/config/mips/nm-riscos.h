// OBSOLETE /* This program is free software; you can redistribute it and/or modify
// OBSOLETE    it under the terms of the GNU General Public License as published by
// OBSOLETE    the Free Software Foundation; either version 2 of the License, or
// OBSOLETE    (at your option) any later version.
// OBSOLETE 
// OBSOLETE    This program is distributed in the hope that it will be useful,
// OBSOLETE    but WITHOUT ANY WARRANTY; without even the implied warranty of
// OBSOLETE    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// OBSOLETE    GNU General Public License for more details.
// OBSOLETE 
// OBSOLETE    You should have received a copy of the GNU General Public License
// OBSOLETE    along with this program; if not, write to the Free Software
// OBSOLETE    Foundation, Inc., 59 Temple Place - Suite 330,
// OBSOLETE    Boston, MA 02111-1307, USA.  */
// OBSOLETE 
// OBSOLETE /* MIPS running RISC/os 4.52C.  */
// OBSOLETE 
// OBSOLETE #define PCB_OFFSET(FIELD) ((int)&((struct user*)0)->u_pcb.FIELD)
// OBSOLETE 
// OBSOLETE /* RISC/os 5.0 defines this in machparam.h.  */
// OBSOLETE #include <bsd43/machine/machparam.h>
// OBSOLETE #define NBPG BSD43_NBPG
// OBSOLETE #define UPAGES BSD43_UPAGES
// OBSOLETE 
// OBSOLETE /* Where is this used?  I don't see any uses in mips-nat.c, and I don't think
// OBSOLETE    the uses in infptrace.c are used if FETCH_INFERIOR_REGISTERS is defined.
// OBSOLETE    Does the compiler react badly to "extern CORE_ADDR kernel_u_addr" (even
// OBSOLETE    if never referenced)?  */
// OBSOLETE #define KERNEL_U_ADDR BSD43_UADDR
// OBSOLETE 
// OBSOLETE #define REGISTER_U_ADDR(addr, blockend, regno) 		\
// OBSOLETE 	      if (regno < FP0_REGNUM) \
// OBSOLETE 		  addr =  UPAGES*NBPG-EF_SIZE+4*((regno)+EF_AT-1); \
// OBSOLETE 	      else if (regno < PC_REGNUM) \
// OBSOLETE 		  addr = PCB_OFFSET(pcb_fpregs[0]) + 4*(regno-FP0_REGNUM); \
// OBSOLETE               else if (regno == PS_REGNUM) \
// OBSOLETE                   addr = UPAGES*NBPG-EF_SIZE+4*EF_SR; \
// OBSOLETE               else if (regno == mips_regnum (current_gdbarch)->badvaddr) \
// OBSOLETE   		  addr = UPAGES*NBPG-EF_SIZE+4*EF_BADVADDR; \
// OBSOLETE 	      else if (regno == mips_regnum (current_gdbarch)->lo) \
// OBSOLETE 		  addr = UPAGES*NBPG-EF_SIZE+4*EF_MDLO; \
// OBSOLETE 	      else if (regno == mips_regnum (current_gdbarch)->hi) \
// OBSOLETE 		  addr = UPAGES*NBPG-EF_SIZE+4*EF_MDHI; \
// OBSOLETE 	      else if (regno == mips_regnum (current_gdbarch)->cause) \
// OBSOLETE 		  addr = UPAGES*NBPG-EF_SIZE+4*EF_CAUSE; \
// OBSOLETE 	      else if (regno == mips_regnum (current_gdbarch)->pc) \
// OBSOLETE 		  addr = UPAGES*NBPG-EF_SIZE+4*EF_EPC; \
// OBSOLETE               else if (regno < mips_regnum (current_gdbarch)->fp_control_status) \
// OBSOLETE 		  addr = PCB_OFFSET(pcb_fpregs[0]) + 4*(regno-FP0_REGNUM); \
// OBSOLETE 	      else if (regno == mips_regnum (current_gdbarch)->fp_control_status) \
// OBSOLETE 		  addr = PCB_OFFSET(pcb_fpc_csr); \
// OBSOLETE 	      else if (regno == mips_regnum (current_gdbarch)->fp_implementation_revision) \
// OBSOLETE 		  addr = PCB_OFFSET(pcb_fpc_eir); \
// OBSOLETE               else \
// OBSOLETE                   addr = 0;
// OBSOLETE 
// OBSOLETE #include "mips/nm-mips.h"
// OBSOLETE 
// OBSOLETE /* Override copies of {fetch,store}_inferior_registers in infptrace.c.  */
// OBSOLETE #define FETCH_INFERIOR_REGISTERS
