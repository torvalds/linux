/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ALPHA_WILDFIRE__H__
#define __ALPHA_WILDFIRE__H__

#include <linux/types.h>
#include <asm/compiler.h>

#define WILDFIRE_MAX_QBB	8	/* more than 8 requires other mods */
#define WILDFIRE_PCA_PER_QBB	4
#define WILDFIRE_IRQ_PER_PCA	64

#define WILDFIRE_NR_IRQS \
  (WILDFIRE_MAX_QBB * WILDFIRE_PCA_PER_QBB * WILDFIRE_IRQ_PER_PCA)

extern unsigned char wildfire_hard_qbb_map[WILDFIRE_MAX_QBB];
extern unsigned char wildfire_soft_qbb_map[WILDFIRE_MAX_QBB];
#define QBB_MAP_EMPTY	0xff

extern unsigned long wildfire_hard_qbb_mask;
extern unsigned long wildfire_soft_qbb_mask;
extern unsigned long wildfire_gp_mask;
extern unsigned long wildfire_hs_mask;
extern unsigned long wildfire_iop_mask;
extern unsigned long wildfire_ior_mask;
extern unsigned long wildfire_pca_mask;
extern unsigned long wildfire_cpu_mask;
extern unsigned long wildfire_mem_mask;

#define WILDFIRE_QBB_EXISTS(qbbno) (wildfire_soft_qbb_mask & (1 << (qbbno)))

#define WILDFIRE_MEM_EXISTS(qbbno) (wildfire_mem_mask & (0xf << ((qbbno) << 2)))

#define WILDFIRE_PCA_EXISTS(qbbno, pcano) \
		(wildfire_pca_mask & (1 << (((qbbno) << 2) + (pcano))))

typedef struct {
	volatile unsigned long csr __attribute__((aligned(64)));
} wildfire_64;

typedef struct {
	volatile unsigned long csr __attribute__((aligned(256)));
} wildfire_256;

typedef struct {
	volatile unsigned long csr __attribute__((aligned(2048)));
} wildfire_2k;

typedef struct {
	wildfire_64	qsd_whami;
	wildfire_64	qsd_rev;
	wildfire_64	qsd_port_present;
	wildfire_64	qsd_port_active;
	wildfire_64	qsd_fault_ena;
	wildfire_64	qsd_cpu_int_ena;
	wildfire_64	qsd_mem_config;
	wildfire_64	qsd_err_sum;
	wildfire_64	ce_sum[4];
	wildfire_64	dev_init[4];
	wildfire_64	it_int[4];
	wildfire_64	ip_int[4];
	wildfire_64	uce_sum[4];
	wildfire_64	se_sum__non_dev_int[4];
	wildfire_64	scratch[4];
	wildfire_64	qsd_timer;
	wildfire_64	qsd_diag;
} wildfire_qsd;

typedef struct {
	wildfire_256	qsd_whami;
	wildfire_256	__pad1;
	wildfire_256	ce_sum;
	wildfire_256	dev_init;
	wildfire_256	it_int;
	wildfire_256	ip_int;
	wildfire_256	uce_sum;
	wildfire_256	se_sum;
} wildfire_fast_qsd;

typedef struct {
	wildfire_2k	qsa_qbb_id;
	wildfire_2k	__pad1;
	wildfire_2k	qsa_port_ena;
	wildfire_2k	qsa_scratch;
	wildfire_2k	qsa_config[5];
	wildfire_2k	qsa_ref_int;
	wildfire_2k	qsa_qbb_pop[2];
	wildfire_2k	qsa_dtag_fc;
	wildfire_2k	__pad2[3];
	wildfire_2k	qsa_diag;
	wildfire_2k	qsa_diag_lock[4];
	wildfire_2k	__pad3[11];
	wildfire_2k	qsa_cpu_err_sum;
	wildfire_2k	qsa_misc_err_sum;
	wildfire_2k	qsa_tmo_err_sum;
	wildfire_2k	qsa_err_ena;
	wildfire_2k	qsa_tmo_config;
	wildfire_2k	qsa_ill_cmd_err_sum;
	wildfire_2k	__pad4[26];
	wildfire_2k	qsa_busy_mask;
	wildfire_2k	qsa_arr_valid;
	wildfire_2k	__pad5[2];
	wildfire_2k	qsa_port_map[4];
	wildfire_2k	qsa_arr_addr[8];
	wildfire_2k	qsa_arr_mask[8];
} wildfire_qsa;

typedef struct {
	wildfire_64	ioa_config;
	wildfire_64	iod_config;
	wildfire_64	iop_switch_credits;
	wildfire_64	__pad1;
	wildfire_64	iop_hose_credits;
	wildfire_64	__pad2[11];
	struct {
		wildfire_64	__pad3;
		wildfire_64	init;
	} iop_hose[4];
	wildfire_64	ioa_hose_0_ctrl;
	wildfire_64	iod_hose_0_ctrl;
	wildfire_64	ioa_hose_1_ctrl;
	wildfire_64	iod_hose_1_ctrl;
	wildfire_64	ioa_hose_2_ctrl;
	wildfire_64	iod_hose_2_ctrl;
	wildfire_64	ioa_hose_3_ctrl;
	wildfire_64	iod_hose_3_ctrl;
	struct {
		wildfire_64	target;
		wildfire_64	__pad4;
	} iop_dev_int[4];

	wildfire_64	iop_err_int_target;
	wildfire_64	__pad5[7];
	wildfire_64	iop_qbb_err_sum;
	wildfire_64	__pad6;
	wildfire_64	iop_qbb_se_sum;
	wildfire_64	__pad7;
	wildfire_64	ioa_err_sum;
	wildfire_64	iod_err_sum;
	wildfire_64	__pad8[4];
	wildfire_64	ioa_diag_force_err;
	wildfire_64	iod_diag_force_err;
	wildfire_64	__pad9[4];
	wildfire_64	iop_diag_send_err_int;
	wildfire_64	__pad10[15];
	wildfire_64	ioa_scratch;
	wildfire_64	iod_scratch;
} wildfire_iop;

typedef struct {
	wildfire_2k	gpa_qbb_map[4];
	wildfire_2k	gpa_mem_pop_map;
	wildfire_2k	gpa_scratch;
	wildfire_2k	gpa_diag;
	wildfire_2k	gpa_config_0;
	wildfire_2k	__pad1;
	wildfire_2k	gpa_init_id;
	wildfire_2k	gpa_config_2;
	/* not complete */
} wildfire_gp;

typedef struct {
	wildfire_64	pca_what_am_i;
	wildfire_64	pca_err_sum;
	wildfire_64	pca_diag_force_err;
	wildfire_64	pca_diag_send_err_int;
	wildfire_64	pca_hose_credits;
	wildfire_64	pca_scratch;
	wildfire_64	pca_micro_addr;
	wildfire_64	pca_micro_data;
	wildfire_64	pca_pend_int;
	wildfire_64	pca_sent_int;
	wildfire_64	__pad1;
	wildfire_64	pca_stdio_edge_level;
	wildfire_64	__pad2[52];
	struct {
		wildfire_64	target;
		wildfire_64	enable;
	} pca_int[4];
	wildfire_64	__pad3[56];
	wildfire_64	pca_alt_sent_int[32];
} wildfire_pca;

typedef struct {
	wildfire_64	ne_what_am_i;
	/* not complete */
} wildfire_ne;

typedef struct {
	wildfire_64	fe_what_am_i;
	/* not complete */
} wildfire_fe;

typedef struct {
	wildfire_64	pci_io_addr_ext;
	wildfire_64	pci_ctrl;
	wildfire_64	pci_err_sum;
	wildfire_64	pci_err_addr;
	wildfire_64	pci_stall_cnt;
	wildfire_64	pci_iack_special;
	wildfire_64	__pad1[2];
	wildfire_64	pci_pend_int;
	wildfire_64	pci_sent_int;
	wildfire_64	__pad2[54];
	struct {
		wildfire_64	wbase;
		wildfire_64	wmask;
		wildfire_64	tbase;
	} pci_window[4];
	wildfire_64	pci_flush_tlb;
	wildfire_64	pci_perf_mon;
} wildfire_pci;

#define WILDFIRE_ENTITY_SHIFT		18

#define WILDFIRE_GP_ENTITY		(0x10UL << WILDFIRE_ENTITY_SHIFT)
#define WILDFIRE_IOP_ENTITY		(0x08UL << WILDFIRE_ENTITY_SHIFT)
#define WILDFIRE_QSA_ENTITY		(0x04UL << WILDFIRE_ENTITY_SHIFT)
#define WILDFIRE_QSD_ENTITY_SLOW	(0x05UL << WILDFIRE_ENTITY_SHIFT)
#define WILDFIRE_QSD_ENTITY_FAST	(0x01UL << WILDFIRE_ENTITY_SHIFT)

#define WILDFIRE_PCA_ENTITY(pca)	((0xc|(pca))<<WILDFIRE_ENTITY_SHIFT)

#define WILDFIRE_BASE		(IDENT_ADDR | (1UL << 40))

#define WILDFIRE_QBB_MASK	0x0fUL	/* for now, only 4 bits/16 QBBs */

#define WILDFIRE_QBB(q)		((~((long)(q)) & WILDFIRE_QBB_MASK) << 36)
#define WILDFIRE_HOSE(h)	((long)(h) << 33)

#define WILDFIRE_QBB_IO(q)	(WILDFIRE_BASE | WILDFIRE_QBB(q))
#define WILDFIRE_QBB_HOSE(q,h)	(WILDFIRE_QBB_IO(q) | WILDFIRE_HOSE(h))

#define WILDFIRE_MEM(q,h)	(WILDFIRE_QBB_HOSE(q,h) | 0x000000000UL)
#define WILDFIRE_CONF(q,h)	(WILDFIRE_QBB_HOSE(q,h) | 0x1FE000000UL)
#define WILDFIRE_IO(q,h)	(WILDFIRE_QBB_HOSE(q,h) | 0x1FF000000UL)

#define WILDFIRE_qsd(q) \
 ((wildfire_qsd *)(WILDFIRE_QBB_IO(q)|WILDFIRE_QSD_ENTITY_SLOW|(((1UL<<13)-1)<<23)))

#define WILDFIRE_fast_qsd() \
 ((wildfire_fast_qsd *)(WILDFIRE_QBB_IO(0)|WILDFIRE_QSD_ENTITY_FAST|(((1UL<<13)-1)<<23)))

#define WILDFIRE_qsa(q) \
 ((wildfire_qsa *)(WILDFIRE_QBB_IO(q)|WILDFIRE_QSA_ENTITY|(((1UL<<13)-1)<<23)))

#define WILDFIRE_iop(q) \
 ((wildfire_iop *)(WILDFIRE_QBB_IO(q)|WILDFIRE_IOP_ENTITY|(((1UL<<13)-1)<<23)))

#define WILDFIRE_gp(q) \
 ((wildfire_gp *)(WILDFIRE_QBB_IO(q)|WILDFIRE_GP_ENTITY|(((1UL<<13)-1)<<23)))

#define WILDFIRE_pca(q,pca) \
 ((wildfire_pca *)(WILDFIRE_QBB_IO(q)|WILDFIRE_PCA_ENTITY(pca)|(((1UL<<13)-1)<<23)))

#define WILDFIRE_ne(q,pca) \
 ((wildfire_ne *)(WILDFIRE_QBB_IO(q)|WILDFIRE_PCA_ENTITY(pca)|(((1UL<<13)-1)<<23)|(1UL<<16)))

#define WILDFIRE_fe(q,pca) \
 ((wildfire_fe *)(WILDFIRE_QBB_IO(q)|WILDFIRE_PCA_ENTITY(pca)|(((1UL<<13)-1)<<23)|(3UL<<15)))

#define WILDFIRE_pci(q,h) \
 ((wildfire_pci *)(WILDFIRE_QBB_IO(q)|WILDFIRE_PCA_ENTITY(((h)&6)>>1)|((((h)&1)|2)<<16)|(((1UL<<13)-1)<<23)))

#define WILDFIRE_IO_BIAS        WILDFIRE_IO(0,0)
#define WILDFIRE_MEM_BIAS       WILDFIRE_MEM(0,0) /* ??? */

/* The IO address space is larger than 0xffff */
#define WILDFIRE_IO_SPACE	(8UL*1024*1024)

#ifdef __KERNEL__

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __IO_EXTERN_INLINE
#endif

/*
 * Memory functions.  all accesses are done through linear space.
 */

__EXTERN_INLINE void __iomem *wildfire_ioportmap(unsigned long addr)
{
	return (void __iomem *)(addr + WILDFIRE_IO_BIAS);
}

__EXTERN_INLINE void __iomem *wildfire_ioremap(unsigned long addr, 
					       unsigned long size)
{
	return (void __iomem *)(addr + WILDFIRE_MEM_BIAS);
}

__EXTERN_INLINE int wildfire_is_ioaddr(unsigned long addr)
{
	return addr >= WILDFIRE_BASE;
}

__EXTERN_INLINE int wildfire_is_mmio(const volatile void __iomem *xaddr)
{
	unsigned long addr = (unsigned long)xaddr;
	return (addr & 0x100000000UL) == 0;
}

#undef __IO_PREFIX
#define __IO_PREFIX			wildfire
#define wildfire_trivial_rw_bw		1
#define wildfire_trivial_rw_lq		1
#define wildfire_trivial_io_bw		1
#define wildfire_trivial_io_lq		1
#define wildfire_trivial_iounmap	1
#include <asm/io_trivial.h>

#ifdef __IO_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_WILDFIRE__H__ */
