/* Save all processor states
 *
 * Copyright (c) 2007 Fleming Feng <fleming.feng@intel.com>
 * Copyright (c) 2007 Anthony Xu   <anthony.xu@intel.com>
 */

#include <asm/asmmacro.h>
#include "asm-offsets.h"


#define CTX(name)    VMM_CTX_##name##_OFFSET

	/*
	 *	r32:		context_t base address
	 */
#define	SAVE_BRANCH_REGS			\
	add	r2 = CTX(B0),r32;		\
	add	r3 = CTX(B1),r32;		\
	mov	r16 = b0;			\
	mov	r17 = b1;			\
	;;					\
	st8	[r2]=r16,16;			\
	st8	[r3]=r17,16;			\
	;;					\
	mov	r16 = b2;			\
	mov	r17 = b3;			\
	;;					\
	st8	[r2]=r16,16;			\
	st8	[r3]=r17,16;			\
	;;					\
	mov	r16 = b4;			\
	mov	r17 = b5;			\
	;;					\
	st8	[r2]=r16;   			\
	st8	[r3]=r17;   			\
	;;

	/*
	 *	r33:		context_t base address
	 */
#define	RESTORE_BRANCH_REGS			\
	add	r2 = CTX(B0),r33;		\
	add	r3 = CTX(B1),r33;		\
	;;					\
	ld8	r16=[r2],16;			\
	ld8	r17=[r3],16;			\
	;;					\
	mov	b0 = r16;			\
	mov	b1 = r17;			\
	;;					\
	ld8	r16=[r2],16;			\
	ld8	r17=[r3],16;			\
	;;					\
	mov	b2 = r16;			\
	mov	b3 = r17;			\
	;;					\
	ld8	r16=[r2];   			\
	ld8	r17=[r3];   			\
	;;					\
	mov	b4=r16;				\
	mov	b5=r17;				\
	;;


	/*
	 *	r32: context_t base address
	 *	bsw == 1
	 *	Save all bank1 general registers, r4 ~ r7
	 */
#define	SAVE_GENERAL_REGS			\
	add	r2=CTX(R4),r32;			\
	add	r3=CTX(R5),r32;			\
	;;					\
.mem.offset 0,0;        			\
	st8.spill	[r2]=r4,16;		\
.mem.offset 8,0;        			\
	st8.spill	[r3]=r5,16;		\
	;;					\
.mem.offset 0,0;        			\
	st8.spill	[r2]=r6,48;		\
.mem.offset 8,0;        			\
	st8.spill	[r3]=r7,48;		\
	;;                          		\
.mem.offset 0,0;        			\
    st8.spill    [r2]=r12;			\
.mem.offset 8,0;				\
    st8.spill    [r3]=r13;			\
    ;;

	/*
	 *	r33: context_t base address
	 *	bsw == 1
	 */
#define	RESTORE_GENERAL_REGS			\
	add	r2=CTX(R4),r33;			\
	add	r3=CTX(R5),r33;			\
	;;					\
	ld8.fill	r4=[r2],16;		\
	ld8.fill	r5=[r3],16;		\
	;;					\
	ld8.fill	r6=[r2],48;		\
	ld8.fill	r7=[r3],48;		\
	;;					\
	ld8.fill    r12=[r2];			\
	ld8.fill    r13 =[r3];			\
	;;




	/*
	 *	r32:		context_t base address
	 */
#define	SAVE_KERNEL_REGS			\
	add	r2 = CTX(KR0),r32;		\
	add	r3 = CTX(KR1),r32;		\
	mov	r16 = ar.k0;			\
	mov	r17 = ar.k1;			\
	;;		        		\
	st8	[r2] = r16,16;			\
	st8	[r3] = r17,16;			\
	;;		        		\
	mov	r16 = ar.k2;			\
	mov	r17 = ar.k3;			\
	;;		        		\
	st8	[r2] = r16,16;			\
	st8	[r3] = r17,16;			\
	;;					\
	mov	r16 = ar.k4;			\
	mov	r17 = ar.k5;			\
	;;				    	\
	st8	[r2] = r16,16;			\
	st8	[r3] = r17,16;			\
	;;					\
	mov	r16 = ar.k6;			\
	mov	r17 = ar.k7;			\
	;;		    			\
	st8	[r2] = r16;     		\
	st8	[r3] = r17;			\
	;;



	/*
	 *	r33:		context_t base address
	 */
#define	RESTORE_KERNEL_REGS			\
	add	r2 = CTX(KR0),r33;		\
	add	r3 = CTX(KR1),r33;		\
	;;		    			\
	ld8	r16=[r2],16;     		\
	ld8	r17=[r3],16;			\
	;;					\
	mov	ar.k0=r16;  			\
	mov	ar.k1=r17;	    		\
	;;		        		\
	ld8	r16=[r2],16;			\
	ld8	r17=[r3],16;			\
	;;		        		\
	mov	ar.k2=r16;   			\
	mov	ar.k3=r17;	    		\
	;;		        		\
	ld8	r16=[r2],16;			\
	ld8	r17=[r3],16;			\
	;;					\
	mov	ar.k4=r16;			\
	mov	ar.k5=r17;	    		\
	;;				    	\
	ld8	r16=[r2],16;			\
	ld8	r17=[r3],16;			\
	;;					\
	mov	ar.k6=r16;  			\
	mov	ar.k7=r17;	    		\
	;;



	/*
	 *	r32:		context_t base address
	 */
#define	SAVE_APP_REGS				\
	add  r2 = CTX(BSPSTORE),r32;		\
	mov  r16 = ar.bspstore;			\
	;;					\
	st8  [r2] = r16,CTX(RNAT)-CTX(BSPSTORE);\
	mov  r16 = ar.rnat;			\
	;;					\
	st8  [r2] = r16,CTX(FCR)-CTX(RNAT);	\
	mov  r16 = ar.fcr;			\
	;;					\
	st8  [r2] = r16,CTX(EFLAG)-CTX(FCR);	\
	mov  r16 = ar.eflag;			\
	;;					\
	st8  [r2] = r16,CTX(CFLG)-CTX(EFLAG);	\
	mov  r16 = ar.cflg;			\
	;;					\
	st8  [r2] = r16,CTX(FSR)-CTX(CFLG);	\
	mov  r16 = ar.fsr;			\
	;;					\
	st8  [r2] = r16,CTX(FIR)-CTX(FSR);	\
	mov  r16 = ar.fir;			\
	;;					\
	st8  [r2] = r16,CTX(FDR)-CTX(FIR);	\
	mov  r16 = ar.fdr;			\
	;;					\
	st8  [r2] = r16,CTX(UNAT)-CTX(FDR);	\
	mov  r16 = ar.unat;			\
	;;					\
	st8  [r2] = r16,CTX(FPSR)-CTX(UNAT);	\
	mov  r16 = ar.fpsr;			\
	;;					\
	st8  [r2] = r16,CTX(PFS)-CTX(FPSR);	\
	mov  r16 = ar.pfs;			\
	;;					\
	st8  [r2] = r16,CTX(LC)-CTX(PFS);	\
	mov  r16 = ar.lc;			\
	;;					\
	st8  [r2] = r16;			\
	;;

	/*
	 *	r33:		context_t base address
	 */
#define	RESTORE_APP_REGS			\
	add  r2=CTX(BSPSTORE),r33;		\
	;;					\
	ld8  r16=[r2],CTX(RNAT)-CTX(BSPSTORE);	\
	;;					\
	mov  ar.bspstore=r16;			\
	ld8  r16=[r2],CTX(FCR)-CTX(RNAT);	\
	;;					\
	mov  ar.rnat=r16;			\
	ld8  r16=[r2],CTX(EFLAG)-CTX(FCR);	\
	;;					\
	mov  ar.fcr=r16;			\
	ld8  r16=[r2],CTX(CFLG)-CTX(EFLAG);	\
	;;					\
	mov  ar.eflag=r16;			\
	ld8  r16=[r2],CTX(FSR)-CTX(CFLG);	\
	;;					\
	mov  ar.cflg=r16;			\
	ld8  r16=[r2],CTX(FIR)-CTX(FSR);	\
	;;					\
	mov  ar.fsr=r16;			\
	ld8  r16=[r2],CTX(FDR)-CTX(FIR);	\
	;;					\
	mov  ar.fir=r16;			\
	ld8  r16=[r2],CTX(UNAT)-CTX(FDR);	\
	;;					\
	mov  ar.fdr=r16;			\
	ld8  r16=[r2],CTX(FPSR)-CTX(UNAT);	\
	;;					\
	mov  ar.unat=r16;			\
	ld8  r16=[r2],CTX(PFS)-CTX(FPSR);	\
	;;					\
	mov  ar.fpsr=r16;			\
	ld8  r16=[r2],CTX(LC)-CTX(PFS);		\
	;;					\
	mov  ar.pfs=r16;			\
	ld8  r16=[r2];				\
	;;					\
	mov  ar.lc=r16;				\
	;;

	/*
	 *	r32:		context_t base address
	 */
#define	SAVE_CTL_REGS				\
	add	r2 = CTX(DCR),r32;		\
	mov	r16 = cr.dcr;			\
	;;					\
	st8	[r2] = r16,CTX(IVA)-CTX(DCR);	\
	;;                          		\
	mov	r16 = cr.iva;			\
	;;					\
	st8	[r2] = r16,CTX(PTA)-CTX(IVA);	\
	;;					\
	mov r16 = cr.pta;			\
	;;					\
	st8 [r2] = r16 ;			\
	;;

	/*
	 *	r33:		context_t base address
	 */
#define	RESTORE_CTL_REGS				\
	add	r2 = CTX(DCR),r33;	        	\
	;;						\
	ld8	r16 = [r2],CTX(IVA)-CTX(DCR);		\
	;;                      			\
	mov	cr.dcr = r16;				\
	dv_serialize_data;				\
	;;						\
	ld8	r16 = [r2],CTX(PTA)-CTX(IVA);		\
	;;						\
	mov	cr.iva = r16;				\
	dv_serialize_data;				\
	;;						\
	ld8 r16 = [r2];					\
	;;						\
	mov cr.pta = r16;				\
	dv_serialize_data;				\
	;;


	/*
	 *	r32:		context_t base address
	 */
#define	SAVE_REGION_REGS			\
	add	r2=CTX(RR0),r32;		\
	mov	r16=rr[r0];			\
	dep.z	r18=1,61,3;			\
	;;					\
	st8	[r2]=r16,8;			\
	mov	r17=rr[r18];			\
	dep.z	r18=2,61,3;			\
	;;					\
	st8	[r2]=r17,8;			\
	mov	r16=rr[r18];			\
	dep.z	r18=3,61,3;			\
	;;					\
	st8	[r2]=r16,8;			\
	mov	r17=rr[r18];			\
	dep.z	r18=4,61,3;			\
	;;					\
	st8	[r2]=r17,8;			\
	mov	r16=rr[r18];			\
	dep.z	r18=5,61,3;			\
	;;					\
	st8	[r2]=r16,8;			\
	mov	r17=rr[r18];			\
	dep.z	r18=7,61,3;			\
	;;					\
	st8	[r2]=r17,16;			\
	mov	r16=rr[r18];			\
	;;					\
	st8	[r2]=r16,8;			\
	;;

	/*
	 *	r33:context_t base address
	 */
#define	RESTORE_REGION_REGS	\
	add	r2=CTX(RR0),r33;\
	mov r18=r0;		\
	;;			\
	ld8	r20=[r2],8;	\
	;;	/* rr0 */	\
	ld8	r21=[r2],8;	\
	;;	/* rr1 */	\
	ld8	r22=[r2],8;	\
	;;	/* rr2 */	\
	ld8	r23=[r2],8;	\
	;;	/* rr3 */	\
	ld8	r24=[r2],8;	\
	;;	/* rr4 */	\
	ld8	r25=[r2],16;	\
	;;	/* rr5 */	\
	ld8	r27=[r2];	\
	;;	/* rr7 */	\
	mov rr[r18]=r20;	\
	dep.z	r18=1,61,3;	\
	;;  /* rr1 */		\
	mov rr[r18]=r21;	\
	dep.z	r18=2,61,3;	\
	;;  /* rr2 */		\
	mov rr[r18]=r22;	\
	dep.z	r18=3,61,3;	\
	;;  /* rr3 */		\
	mov rr[r18]=r23;	\
	dep.z	r18=4,61,3;	\
	;;  /* rr4 */		\
	mov rr[r18]=r24;	\
	dep.z	r18=5,61,3;	\
	;;  /* rr5 */		\
	mov rr[r18]=r25;	\
	dep.z	r18=7,61,3;	\
	;;  /* rr7 */		\
	mov rr[r18]=r27;	\
	;;			\
	srlz.i;			\
	;;



	/*
	 *	r32:	context_t base address
	 *	r36~r39:scratch registers
	 */
#define	SAVE_DEBUG_REGS				\
	add	r2=CTX(IBR0),r32;		\
	add	r3=CTX(DBR0),r32;		\
	mov	r16=ibr[r0];			\
	mov	r17=dbr[r0];			\
	;;					\
	st8	[r2]=r16,8; 			\
	st8	[r3]=r17,8;	    		\
	add	r18=1,r0;		    	\
	;;					\
	mov	r16=ibr[r18];			\
	mov	r17=dbr[r18];			\
	;;					\
	st8	[r2]=r16,8;		    	\
	st8	[r3]=r17,8;			\
	add	r18=2,r0;			\
	;;					\
	mov	r16=ibr[r18];			\
	mov	r17=dbr[r18];			\
	;;					\
	st8	[r2]=r16,8;		    	\
	st8	[r3]=r17,8;			\
	add	r18=2,r0;			\
	;;					\
	mov	r16=ibr[r18];			\
	mov	r17=dbr[r18];			\
	;;					\
	st8	[r2]=r16,8;		    	\
	st8	[r3]=r17,8;			\
	add	r18=3,r0;			\
	;;					\
	mov	r16=ibr[r18];			\
	mov	r17=dbr[r18];			\
	;;					\
	st8	[r2]=r16,8;		    	\
	st8	[r3]=r17,8;			\
	add	r18=4,r0;			\
	;;					\
	mov	r16=ibr[r18];			\
	mov	r17=dbr[r18];			\
	;;					\
	st8	[r2]=r16,8;		    	\
	st8	[r3]=r17,8;			\
	add	r18=5,r0;			\
	;;					\
	mov	r16=ibr[r18];			\
	mov	r17=dbr[r18];			\
	;;					\
	st8	[r2]=r16,8;		    	\
	st8	[r3]=r17,8;			\
	add	r18=6,r0;			\
	;;					\
	mov	r16=ibr[r18];			\
	mov	r17=dbr[r18];			\
	;;					\
	st8	[r2]=r16,8;		    	\
	st8	[r3]=r17,8;			\
	add	r18=7,r0;			\
	;;					\
	mov	r16=ibr[r18];			\
	mov	r17=dbr[r18];			\
	;;					\
	st8	[r2]=r16,8;		    	\
	st8	[r3]=r17,8;			\
	;;


/*
 *      r33:    point to context_t structure
 *      ar.lc are corrupted.
 */
#define RESTORE_DEBUG_REGS			\
	add	r2=CTX(IBR0),r33;		\
	add	r3=CTX(DBR0),r33;		\
	mov r16=7;    				\
	mov r17=r0;				\
	;;                    			\
	mov ar.lc = r16;			\
	;; 					\
1:						\
	ld8 r18=[r2],8;		    		\
	ld8 r19=[r3],8;				\
	;;					\
	mov ibr[r17]=r18;			\
	mov dbr[r17]=r19;			\
	;;   					\
	srlz.i;					\
	;; 					\
	add r17=1,r17;				\
	br.cloop.sptk 1b;			\
	;;


	/*
	 *	r32:		context_t base address
	 */
#define	SAVE_FPU_LOW				\
	add	r2=CTX(F2),r32;			\
	add	r3=CTX(F3),r32;			\
	;;					\
	stf.spill.nta	[r2]=f2,32;		\
	stf.spill.nta	[r3]=f3,32;		\
	;;					\
	stf.spill.nta	[r2]=f4,32;		\
	stf.spill.nta	[r3]=f5,32;		\
	;;					\
	stf.spill.nta	[r2]=f6,32;		\
	stf.spill.nta	[r3]=f7,32;		\
	;;					\
	stf.spill.nta	[r2]=f8,32;		\
	stf.spill.nta	[r3]=f9,32;		\
	;;					\
	stf.spill.nta	[r2]=f10,32;		\
	stf.spill.nta	[r3]=f11,32;		\
	;;					\
	stf.spill.nta	[r2]=f12,32;		\
	stf.spill.nta	[r3]=f13,32;		\
	;;					\
	stf.spill.nta	[r2]=f14,32;		\
	stf.spill.nta	[r3]=f15,32;		\
	;;					\
	stf.spill.nta	[r2]=f16,32;		\
	stf.spill.nta	[r3]=f17,32;		\
	;;					\
	stf.spill.nta	[r2]=f18,32;		\
	stf.spill.nta	[r3]=f19,32;		\
	;;					\
	stf.spill.nta	[r2]=f20,32;		\
	stf.spill.nta	[r3]=f21,32;		\
	;;					\
	stf.spill.nta	[r2]=f22,32;		\
	stf.spill.nta	[r3]=f23,32;		\
	;;					\
	stf.spill.nta	[r2]=f24,32;		\
	stf.spill.nta	[r3]=f25,32;		\
	;;					\
	stf.spill.nta	[r2]=f26,32;		\
	stf.spill.nta	[r3]=f27,32;		\
	;;					\
	stf.spill.nta	[r2]=f28,32;		\
	stf.spill.nta	[r3]=f29,32;		\
	;;					\
	stf.spill.nta	[r2]=f30;		\
	stf.spill.nta	[r3]=f31;		\
	;;

	/*
	 *	r32:		context_t base address
	 */
#define	SAVE_FPU_HIGH				\
	add	r2=CTX(F32),r32;		\
	add	r3=CTX(F33),r32;		\
	;;					\
	stf.spill.nta	[r2]=f32,32;		\
	stf.spill.nta	[r3]=f33,32;		\
	;;					\
	stf.spill.nta	[r2]=f34,32;		\
	stf.spill.nta	[r3]=f35,32;		\
	;;					\
	stf.spill.nta	[r2]=f36,32;		\
	stf.spill.nta	[r3]=f37,32;		\
	;;					\
	stf.spill.nta	[r2]=f38,32;		\
	stf.spill.nta	[r3]=f39,32;		\
	;;					\
	stf.spill.nta	[r2]=f40,32;		\
	stf.spill.nta	[r3]=f41,32;		\
	;;					\
	stf.spill.nta	[r2]=f42,32;		\
	stf.spill.nta	[r3]=f43,32;		\
	;;					\
	stf.spill.nta	[r2]=f44,32;		\
	stf.spill.nta	[r3]=f45,32;		\
	;;					\
	stf.spill.nta	[r2]=f46,32;		\
	stf.spill.nta	[r3]=f47,32;		\
	;;					\
	stf.spill.nta	[r2]=f48,32;		\
	stf.spill.nta	[r3]=f49,32;		\
	;;					\
	stf.spill.nta	[r2]=f50,32;		\
	stf.spill.nta	[r3]=f51,32;		\
	;;					\
	stf.spill.nta	[r2]=f52,32;		\
	stf.spill.nta	[r3]=f53,32;		\
	;;					\
	stf.spill.nta	[r2]=f54,32;		\
	stf.spill.nta	[r3]=f55,32;		\
	;;					\
	stf.spill.nta	[r2]=f56,32;		\
	stf.spill.nta	[r3]=f57,32;		\
	;;					\
	stf.spill.nta	[r2]=f58,32;		\
	stf.spill.nta	[r3]=f59,32;		\
	;;					\
	stf.spill.nta	[r2]=f60,32;		\
	stf.spill.nta	[r3]=f61,32;		\
	;;					\
	stf.spill.nta	[r2]=f62,32;		\
	stf.spill.nta	[r3]=f63,32;		\
	;;					\
	stf.spill.nta	[r2]=f64,32;		\
	stf.spill.nta	[r3]=f65,32;		\
	;;					\
	stf.spill.nta	[r2]=f66,32;		\
	stf.spill.nta	[r3]=f67,32;		\
	;;					\
	stf.spill.nta	[r2]=f68,32;		\
	stf.spill.nta	[r3]=f69,32;		\
	;;					\
	stf.spill.nta	[r2]=f70,32;		\
	stf.spill.nta	[r3]=f71,32;		\
	;;					\
	stf.spill.nta	[r2]=f72,32;		\
	stf.spill.nta	[r3]=f73,32;		\
	;;					\
	stf.spill.nta	[r2]=f74,32;		\
	stf.spill.nta	[r3]=f75,32;		\
	;;					\
	stf.spill.nta	[r2]=f76,32;		\
	stf.spill.nta	[r3]=f77,32;		\
	;;					\
	stf.spill.nta	[r2]=f78,32;		\
	stf.spill.nta	[r3]=f79,32;		\
	;;					\
	stf.spill.nta	[r2]=f80,32;		\
	stf.spill.nta	[r3]=f81,32;		\
	;;					\
	stf.spill.nta	[r2]=f82,32;		\
	stf.spill.nta	[r3]=f83,32;		\
	;;					\
	stf.spill.nta	[r2]=f84,32;		\
	stf.spill.nta	[r3]=f85,32;		\
	;;					\
	stf.spill.nta	[r2]=f86,32;		\
	stf.spill.nta	[r3]=f87,32;		\
	;;					\
	stf.spill.nta	[r2]=f88,32;		\
	stf.spill.nta	[r3]=f89,32;		\
	;;					\
	stf.spill.nta	[r2]=f90,32;		\
	stf.spill.nta	[r3]=f91,32;		\
	;;					\
	stf.spill.nta	[r2]=f92,32;		\
	stf.spill.nta	[r3]=f93,32;		\
	;;					\
	stf.spill.nta	[r2]=f94,32;		\
	stf.spill.nta	[r3]=f95,32;		\
	;;					\
	stf.spill.nta	[r2]=f96,32;		\
	stf.spill.nta	[r3]=f97,32;		\
	;;					\
	stf.spill.nta	[r2]=f98,32;		\
	stf.spill.nta	[r3]=f99,32;		\
	;;					\
	stf.spill.nta	[r2]=f100,32;		\
	stf.spill.nta	[r3]=f101,32;		\
	;;					\
	stf.spill.nta	[r2]=f102,32;		\
	stf.spill.nta	[r3]=f103,32;		\
	;;					\
	stf.spill.nta	[r2]=f104,32;		\
	stf.spill.nta	[r3]=f105,32;		\
	;;					\
	stf.spill.nta	[r2]=f106,32;		\
	stf.spill.nta	[r3]=f107,32;		\
	;;					\
	stf.spill.nta	[r2]=f108,32;		\
	stf.spill.nta	[r3]=f109,32;		\
	;;					\
	stf.spill.nta	[r2]=f110,32;		\
	stf.spill.nta	[r3]=f111,32;		\
	;;					\
	stf.spill.nta	[r2]=f112,32;		\
	stf.spill.nta	[r3]=f113,32;		\
	;;					\
	stf.spill.nta	[r2]=f114,32;		\
	stf.spill.nta	[r3]=f115,32;		\
	;;					\
	stf.spill.nta	[r2]=f116,32;		\
	stf.spill.nta	[r3]=f117,32;		\
	;;					\
	stf.spill.nta	[r2]=f118,32;		\
	stf.spill.nta	[r3]=f119,32;		\
	;;					\
	stf.spill.nta	[r2]=f120,32;		\
	stf.spill.nta	[r3]=f121,32;		\
	;;					\
	stf.spill.nta	[r2]=f122,32;		\
	stf.spill.nta	[r3]=f123,32;		\
	;;					\
	stf.spill.nta	[r2]=f124,32;		\
	stf.spill.nta	[r3]=f125,32;		\
	;;					\
	stf.spill.nta	[r2]=f126;		\
	stf.spill.nta	[r3]=f127;		\
	;;

     /*
      *      r33:    point to context_t structure
      */
#define	RESTORE_FPU_LOW				\
    add     r2 = CTX(F2), r33;			\
    add     r3 = CTX(F3), r33;			\
    ;;						\
    ldf.fill.nta f2 = [r2], 32;			\
    ldf.fill.nta f3 = [r3], 32;			\
    ;;						\
    ldf.fill.nta f4 = [r2], 32;			\
    ldf.fill.nta f5 = [r3], 32;			\
    ;;						\
    ldf.fill.nta f6 = [r2], 32;			\
    ldf.fill.nta f7 = [r3], 32;			\
    ;;						\
    ldf.fill.nta f8 = [r2], 32;			\
    ldf.fill.nta f9 = [r3], 32;			\
    ;;						\
    ldf.fill.nta f10 = [r2], 32;		\
    ldf.fill.nta f11 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f12 = [r2], 32;		\
    ldf.fill.nta f13 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f14 = [r2], 32;		\
    ldf.fill.nta f15 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f16 = [r2], 32;		\
    ldf.fill.nta f17 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f18 = [r2], 32;		\
    ldf.fill.nta f19 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f20 = [r2], 32;		\
    ldf.fill.nta f21 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f22 = [r2], 32;		\
    ldf.fill.nta f23 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f24 = [r2], 32;		\
    ldf.fill.nta f25 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f26 = [r2], 32;		\
    ldf.fill.nta f27 = [r3], 32;		\
	;;					\
    ldf.fill.nta f28 = [r2], 32;		\
    ldf.fill.nta f29 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f30 = [r2], 32;		\
    ldf.fill.nta f31 = [r3], 32;		\
    ;;



    /*
     *      r33:    point to context_t structure
     */
#define	RESTORE_FPU_HIGH			\
    add     r2 = CTX(F32), r33;			\
    add     r3 = CTX(F33), r33;			\
    ;;						\
    ldf.fill.nta f32 = [r2], 32;		\
    ldf.fill.nta f33 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f34 = [r2], 32;		\
    ldf.fill.nta f35 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f36 = [r2], 32;		\
    ldf.fill.nta f37 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f38 = [r2], 32;		\
    ldf.fill.nta f39 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f40 = [r2], 32;		\
    ldf.fill.nta f41 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f42 = [r2], 32;		\
    ldf.fill.nta f43 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f44 = [r2], 32;		\
    ldf.fill.nta f45 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f46 = [r2], 32;		\
    ldf.fill.nta f47 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f48 = [r2], 32;		\
    ldf.fill.nta f49 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f50 = [r2], 32;		\
    ldf.fill.nta f51 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f52 = [r2], 32;		\
    ldf.fill.nta f53 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f54 = [r2], 32;		\
    ldf.fill.nta f55 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f56 = [r2], 32;		\
    ldf.fill.nta f57 = [r3], 32;   		\
    ;;						\
    ldf.fill.nta f58 = [r2], 32;		\
    ldf.fill.nta f59 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f60 = [r2], 32;		\
    ldf.fill.nta f61 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f62 = [r2], 32;		\
    ldf.fill.nta f63 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f64 = [r2], 32;		\
    ldf.fill.nta f65 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f66 = [r2], 32;		\
    ldf.fill.nta f67 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f68 = [r2], 32;		\
    ldf.fill.nta f69 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f70 = [r2], 32;		\
    ldf.fill.nta f71 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f72 = [r2], 32;		\
    ldf.fill.nta f73 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f74 = [r2], 32;		\
    ldf.fill.nta f75 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f76 = [r2], 32;		\
    ldf.fill.nta f77 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f78 = [r2], 32;		\
    ldf.fill.nta f79 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f80 = [r2], 32;		\
    ldf.fill.nta f81 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f82 = [r2], 32;		\
    ldf.fill.nta f83 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f84 = [r2], 32;		\
    ldf.fill.nta f85 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f86 = [r2], 32;		\
    ldf.fill.nta f87 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f88 = [r2], 32;		\
    ldf.fill.nta f89 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f90 = [r2], 32;		\
    ldf.fill.nta f91 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f92 = [r2], 32;		\
    ldf.fill.nta f93 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f94 = [r2], 32;		\
    ldf.fill.nta f95 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f96 = [r2], 32;		\
    ldf.fill.nta f97 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f98 = [r2], 32;		\
    ldf.fill.nta f99 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f100 = [r2], 32;		\
    ldf.fill.nta f101 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f102 = [r2], 32;		\
    ldf.fill.nta f103 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f104 = [r2], 32;		\
    ldf.fill.nta f105 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f106 = [r2], 32;		\
    ldf.fill.nta f107 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f108 = [r2], 32;		\
    ldf.fill.nta f109 = [r3], 32;   		\
    ;;						\
    ldf.fill.nta f110 = [r2], 32;		\
    ldf.fill.nta f111 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f112 = [r2], 32;		\
    ldf.fill.nta f113 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f114 = [r2], 32;		\
    ldf.fill.nta f115 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f116 = [r2], 32;		\
    ldf.fill.nta f117 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f118 = [r2], 32;		\
    ldf.fill.nta f119 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f120 = [r2], 32;		\
    ldf.fill.nta f121 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f122 = [r2], 32;		\
    ldf.fill.nta f123 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f124 = [r2], 32;		\
    ldf.fill.nta f125 = [r3], 32;		\
    ;;						\
    ldf.fill.nta f126 = [r2], 32;		\
    ldf.fill.nta f127 = [r3], 32;		\
    ;;

	/*
	 *	r32:		context_t base address
	 */
#define	SAVE_PTK_REGS				\
    add r2=CTX(PKR0), r32;			\
    mov r16=7;    				\
    ;;                         			\
    mov ar.lc=r16;  				\
    mov r17=r0;					\
    ;;						\
1:						\
    mov r18=pkr[r17];				\
    ;;                     			\
    srlz.i;					\
    ;; 						\
    st8 [r2]=r18, 8;				\
    ;;    					\
    add r17 =1,r17;				\
    ;;                     			\
    br.cloop.sptk 1b;				\
    ;;

/*
 *      r33:    point to context_t structure
 *      ar.lc are corrupted.
 */
#define RESTORE_PTK_REGS	    		\
    add r2=CTX(PKR0), r33;			\
    mov r16=7;    				\
    ;;                         			\
    mov ar.lc=r16;  				\
    mov r17=r0;					\
    ;;						\
1: 						\
    ld8 r18=[r2], 8;				\
    ;;						\
    mov pkr[r17]=r18;				\
    ;;    					\
    srlz.i;					\
    ;; 						\
    add r17 =1,r17;				\
    ;;                     			\
    br.cloop.sptk 1b;				\
    ;;


/*
 * void vmm_trampoline( context_t * from,
 *			context_t * to)
 *
 * 	from:	r32
 *	to:	r33
 *  note: interrupt disabled before call this function.
 */
GLOBAL_ENTRY(vmm_trampoline)
    mov r16 = psr
    adds r2 = CTX(PSR), r32
    ;;
    st8 [r2] = r16, 8       // psr
    mov r17 = pr
    ;;
    st8 [r2] = r17, 8       // pr
    mov r18 = ar.unat
    ;;
    st8 [r2] = r18
    mov r17 = ar.rsc
    ;;
    adds r2 = CTX(RSC),r32
    ;;
    st8 [r2]= r17
    mov ar.rsc =0
    flushrs
    ;;
    SAVE_GENERAL_REGS
    ;;
    SAVE_KERNEL_REGS
    ;;
    SAVE_APP_REGS
    ;;
    SAVE_BRANCH_REGS
    ;;
    SAVE_CTL_REGS
    ;;
    SAVE_REGION_REGS
    ;;
    //SAVE_DEBUG_REGS
    ;;
    rsm  psr.dfl
    ;;
    srlz.d
    ;;
    SAVE_FPU_LOW
    ;;
    rsm  psr.dfh
    ;;
    srlz.d
    ;;
    SAVE_FPU_HIGH
    ;;
    SAVE_PTK_REGS
    ;;
    RESTORE_PTK_REGS
    ;;
    RESTORE_FPU_HIGH
    ;;
    RESTORE_FPU_LOW
    ;;
    //RESTORE_DEBUG_REGS
    ;;
    RESTORE_REGION_REGS
    ;;
    RESTORE_CTL_REGS
    ;;
    RESTORE_BRANCH_REGS
    ;;
    RESTORE_APP_REGS
    ;;
    RESTORE_KERNEL_REGS
    ;;
    RESTORE_GENERAL_REGS
    ;;
    adds r2=CTX(PSR), r33
    ;;
    ld8 r16=[r2], 8       // psr
    ;;
    mov psr.l=r16
    ;;
    srlz.d
    ;;
    ld8 r16=[r2], 8       // pr
    ;;
    mov pr =r16,-1
    ld8 r16=[r2]       // unat
    ;;
    mov ar.unat=r16
    ;;
    adds r2=CTX(RSC),r33
    ;;
    ld8 r16 =[r2]
    ;;
    mov ar.rsc = r16
    ;;
    br.ret.sptk.few b0
END(vmm_trampoline)
