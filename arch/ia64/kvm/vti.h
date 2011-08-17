/*
 * vti.h: prototype for generial vt related interface
 *   	Copyright (c) 2004, Intel Corporation.
 *
 *	Xuefei Xu (Anthony Xu) (anthony.xu@intel.com)
 *	Fred Yang (fred.yang@intel.com)
 * 	Kun Tian (Kevin Tian) (kevin.tian@intel.com)
 *
 *  	Copyright (c) 2007, Intel Corporation.
 *  	Zhang xiantao <xiantao.zhang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */
#ifndef _KVM_VT_I_H
#define _KVM_VT_I_H

#ifndef __ASSEMBLY__
#include <asm/page.h>

#include <linux/kvm_host.h>

/* define itr.i and itr.d  in ia64_itr function */
#define	ITR	0x01
#define	DTR	0x02
#define	IaDTR	0x03

#define IA64_TR_VMM       6 /*itr6, dtr6 : maps vmm code, vmbuffer*/
#define IA64_TR_VM_DATA   7 /*dtr7       : maps current vm data*/

#define RR6 (6UL<<61)
#define RR7 (7UL<<61)


/* config_options in pal_vp_init_env */
#define	VP_INITIALIZE	1UL
#define	VP_FR_PMC	1UL<<1
#define	VP_OPCODE	1UL<<8
#define	VP_CAUSE	1UL<<9
#define VP_FW_ACC   	1UL<<63

/* init vp env with initializing vm_buffer */
#define	VP_INIT_ENV_INITALIZE  (VP_INITIALIZE | VP_FR_PMC |\
	VP_OPCODE | VP_CAUSE | VP_FW_ACC)
/* init vp env without initializing vm_buffer */
#define	VP_INIT_ENV  VP_FR_PMC | VP_OPCODE | VP_CAUSE | VP_FW_ACC

#define		PAL_VP_CREATE   265
/* Stacked Virt. Initializes a new VPD for the operation of
 * a new virtual processor in the virtual environment.
 */
#define		PAL_VP_ENV_INFO 266
/*Stacked Virt. Returns the parameters needed to enter a virtual environment.*/
#define		PAL_VP_EXIT_ENV 267
/*Stacked Virt. Allows a logical processor to exit a virtual environment.*/
#define		PAL_VP_INIT_ENV 268
/*Stacked Virt. Allows a logical processor to enter a virtual environment.*/
#define		PAL_VP_REGISTER 269
/*Stacked Virt. Register a different host IVT for the virtual processor.*/
#define		PAL_VP_RESUME   270
/* Renamed from PAL_VP_RESUME */
#define		PAL_VP_RESTORE  270
/*Stacked Virt. Resumes virtual processor operation on the logical processor.*/
#define		PAL_VP_SUSPEND  271
/* Renamed from PAL_VP_SUSPEND */
#define		PAL_VP_SAVE	271
/* Stacked Virt. Suspends operation for the specified virtual processor on
 * the logical processor.
 */
#define		PAL_VP_TERMINATE 272
/* Stacked Virt. Terminates operation for the specified virtual processor.*/

union vac {
	unsigned long value;
	struct {
		unsigned int a_int:1;
		unsigned int a_from_int_cr:1;
		unsigned int a_to_int_cr:1;
		unsigned int a_from_psr:1;
		unsigned int a_from_cpuid:1;
		unsigned int a_cover:1;
		unsigned int a_bsw:1;
		long reserved:57;
	};
};

union vdc {
	unsigned long value;
	struct {
		unsigned int d_vmsw:1;
		unsigned int d_extint:1;
		unsigned int d_ibr_dbr:1;
		unsigned int d_pmc:1;
		unsigned int d_to_pmd:1;
		unsigned int d_itm:1;
		long reserved:58;
	};
};

struct vpd {
	union vac   vac;
	union vdc   vdc;
	unsigned long  virt_env_vaddr;
	unsigned long  reserved1[29];
	unsigned long  vhpi;
	unsigned long  reserved2[95];
	unsigned long  vgr[16];
	unsigned long  vbgr[16];
	unsigned long  vnat;
	unsigned long  vbnat;
	unsigned long  vcpuid[5];
	unsigned long  reserved3[11];
	unsigned long  vpsr;
	unsigned long  vpr;
	unsigned long  reserved4[76];
	union {
		unsigned long  vcr[128];
		struct {
			unsigned long dcr;
			unsigned long itm;
			unsigned long iva;
			unsigned long rsv1[5];
			unsigned long pta;
			unsigned long rsv2[7];
			unsigned long ipsr;
			unsigned long isr;
			unsigned long rsv3;
			unsigned long iip;
			unsigned long ifa;
			unsigned long itir;
			unsigned long iipa;
			unsigned long ifs;
			unsigned long iim;
			unsigned long iha;
			unsigned long rsv4[38];
			unsigned long lid;
			unsigned long ivr;
			unsigned long tpr;
			unsigned long eoi;
			unsigned long irr[4];
			unsigned long itv;
			unsigned long pmv;
			unsigned long cmcv;
			unsigned long rsv5[5];
			unsigned long lrr0;
			unsigned long lrr1;
			unsigned long rsv6[46];
		};
	};
	unsigned long  reserved5[128];
	unsigned long  reserved6[3456];
	unsigned long  vmm_avail[128];
	unsigned long  reserved7[4096];
};

#define PAL_PROC_VM_BIT		(1UL << 40)
#define PAL_PROC_VMSW_BIT	(1UL << 54)

static inline s64 ia64_pal_vp_env_info(u64 *buffer_size,
		u64 *vp_env_info)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_STK(iprv, PAL_VP_ENV_INFO, 0, 0, 0);
	*buffer_size = iprv.v0;
	*vp_env_info = iprv.v1;
	return iprv.status;
}

static inline s64 ia64_pal_vp_exit_env(u64 iva)
{
	struct ia64_pal_retval iprv;

	PAL_CALL_STK(iprv, PAL_VP_EXIT_ENV, (u64)iva, 0, 0);
	return iprv.status;
}

static inline s64 ia64_pal_vp_init_env(u64 config_options, u64 pbase_addr,
			u64 vbase_addr, u64 *vsa_base)
{
	struct ia64_pal_retval iprv;

	PAL_CALL_STK(iprv, PAL_VP_INIT_ENV, config_options, pbase_addr,
			vbase_addr);
	*vsa_base = iprv.v0;

	return iprv.status;
}

static inline s64 ia64_pal_vp_restore(u64 *vpd, u64 pal_proc_vector)
{
	struct ia64_pal_retval iprv;

	PAL_CALL_STK(iprv, PAL_VP_RESTORE, (u64)vpd, pal_proc_vector, 0);

	return iprv.status;
}

static inline s64 ia64_pal_vp_save(u64 *vpd, u64 pal_proc_vector)
{
	struct ia64_pal_retval iprv;

	PAL_CALL_STK(iprv, PAL_VP_SAVE, (u64)vpd, pal_proc_vector, 0);

	return iprv.status;
}

#endif

/*VPD field offset*/
#define VPD_VAC_START_OFFSET		0
#define VPD_VDC_START_OFFSET		8
#define VPD_VHPI_START_OFFSET		256
#define VPD_VGR_START_OFFSET		1024
#define VPD_VBGR_START_OFFSET		1152
#define VPD_VNAT_START_OFFSET		1280
#define VPD_VBNAT_START_OFFSET		1288
#define VPD_VCPUID_START_OFFSET		1296
#define VPD_VPSR_START_OFFSET		1424
#define VPD_VPR_START_OFFSET		1432
#define VPD_VRSE_CFLE_START_OFFSET	1440
#define VPD_VCR_START_OFFSET		2048
#define VPD_VTPR_START_OFFSET		2576
#define VPD_VRR_START_OFFSET		3072
#define VPD_VMM_VAIL_START_OFFSET	31744

/*Virtualization faults*/

#define EVENT_MOV_TO_AR			 1
#define EVENT_MOV_TO_AR_IMM		 2
#define EVENT_MOV_FROM_AR		 3
#define EVENT_MOV_TO_CR			 4
#define EVENT_MOV_FROM_CR		 5
#define EVENT_MOV_TO_PSR		 6
#define EVENT_MOV_FROM_PSR		 7
#define EVENT_ITC_D			 8
#define EVENT_ITC_I			 9
#define EVENT_MOV_TO_RR			 10
#define EVENT_MOV_TO_DBR		 11
#define EVENT_MOV_TO_IBR		 12
#define EVENT_MOV_TO_PKR		 13
#define EVENT_MOV_TO_PMC		 14
#define EVENT_MOV_TO_PMD		 15
#define EVENT_ITR_D			 16
#define EVENT_ITR_I			 17
#define EVENT_MOV_FROM_RR		 18
#define EVENT_MOV_FROM_DBR		 19
#define EVENT_MOV_FROM_IBR		 20
#define EVENT_MOV_FROM_PKR		 21
#define EVENT_MOV_FROM_PMC		 22
#define EVENT_MOV_FROM_CPUID		 23
#define EVENT_SSM			 24
#define EVENT_RSM			 25
#define EVENT_PTC_L			 26
#define EVENT_PTC_G			 27
#define EVENT_PTC_GA			 28
#define EVENT_PTR_D			 29
#define EVENT_PTR_I			 30
#define EVENT_THASH			 31
#define EVENT_TTAG			 32
#define EVENT_TPA			 33
#define EVENT_TAK			 34
#define EVENT_PTC_E			 35
#define EVENT_COVER			 36
#define EVENT_RFI			 37
#define EVENT_BSW_0			 38
#define EVENT_BSW_1			 39
#define EVENT_VMSW			 40

/**PAL virtual services offsets */
#define PAL_VPS_RESUME_NORMAL           0x0000
#define PAL_VPS_RESUME_HANDLER          0x0400
#define PAL_VPS_SYNC_READ               0x0800
#define PAL_VPS_SYNC_WRITE              0x0c00
#define PAL_VPS_SET_PENDING_INTERRUPT   0x1000
#define PAL_VPS_THASH                   0x1400
#define PAL_VPS_TTAG                    0x1800
#define PAL_VPS_RESTORE                 0x1c00
#define PAL_VPS_SAVE                    0x2000

#endif/* _VT_I_H*/
