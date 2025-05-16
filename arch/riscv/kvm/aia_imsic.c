// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 *
 * Authors:
 *	Anup Patel <apatel@ventanamicro.com>
 */

#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/irqchip/riscv-imsic.h>
#include <linux/kvm_host.h>
#include <linux/math.h>
#include <linux/spinlock.h>
#include <linux/swab.h>
#include <kvm/iodev.h>
#include <asm/csr.h>

#define IMSIC_MAX_EIX	(IMSIC_MAX_ID / BITS_PER_TYPE(u64))

struct imsic_mrif_eix {
	unsigned long eip[BITS_PER_TYPE(u64) / BITS_PER_LONG];
	unsigned long eie[BITS_PER_TYPE(u64) / BITS_PER_LONG];
};

struct imsic_mrif {
	struct imsic_mrif_eix eix[IMSIC_MAX_EIX];
	unsigned long eithreshold;
	unsigned long eidelivery;
};

struct imsic {
	struct kvm_io_device iodev;

	u32 nr_msis;
	u32 nr_eix;
	u32 nr_hw_eix;

	/*
	 * At any point in time, the register state is in
	 * one of the following places:
	 *
	 * 1) Hardware: IMSIC VS-file (vsfile_cpu >= 0)
	 * 2) Software: IMSIC SW-file (vsfile_cpu < 0)
	 */

	/* IMSIC VS-file */
	rwlock_t vsfile_lock;
	int vsfile_cpu;
	int vsfile_hgei;
	void __iomem *vsfile_va;
	phys_addr_t vsfile_pa;

	/* IMSIC SW-file */
	struct imsic_mrif *swfile;
	phys_addr_t swfile_pa;
	raw_spinlock_t swfile_extirq_lock;
};

#define imsic_vs_csr_read(__c)			\
({						\
	unsigned long __r;			\
	csr_write(CSR_VSISELECT, __c);		\
	__r = csr_read(CSR_VSIREG);		\
	__r;					\
})

#define imsic_read_switchcase(__ireg)			\
	case __ireg:					\
		return imsic_vs_csr_read(__ireg);
#define imsic_read_switchcase_2(__ireg)			\
	imsic_read_switchcase(__ireg + 0)		\
	imsic_read_switchcase(__ireg + 1)
#define imsic_read_switchcase_4(__ireg)			\
	imsic_read_switchcase_2(__ireg + 0)		\
	imsic_read_switchcase_2(__ireg + 2)
#define imsic_read_switchcase_8(__ireg)			\
	imsic_read_switchcase_4(__ireg + 0)		\
	imsic_read_switchcase_4(__ireg + 4)
#define imsic_read_switchcase_16(__ireg)		\
	imsic_read_switchcase_8(__ireg + 0)		\
	imsic_read_switchcase_8(__ireg + 8)
#define imsic_read_switchcase_32(__ireg)		\
	imsic_read_switchcase_16(__ireg + 0)		\
	imsic_read_switchcase_16(__ireg + 16)
#define imsic_read_switchcase_64(__ireg)		\
	imsic_read_switchcase_32(__ireg + 0)		\
	imsic_read_switchcase_32(__ireg + 32)

static unsigned long imsic_eix_read(int ireg)
{
	switch (ireg) {
	imsic_read_switchcase_64(IMSIC_EIP0)
	imsic_read_switchcase_64(IMSIC_EIE0)
	}

	return 0;
}

#define imsic_vs_csr_swap(__c, __v)		\
({						\
	unsigned long __r;			\
	csr_write(CSR_VSISELECT, __c);		\
	__r = csr_swap(CSR_VSIREG, __v);	\
	__r;					\
})

#define imsic_swap_switchcase(__ireg, __v)		\
	case __ireg:					\
		return imsic_vs_csr_swap(__ireg, __v);
#define imsic_swap_switchcase_2(__ireg, __v)		\
	imsic_swap_switchcase(__ireg + 0, __v)		\
	imsic_swap_switchcase(__ireg + 1, __v)
#define imsic_swap_switchcase_4(__ireg, __v)		\
	imsic_swap_switchcase_2(__ireg + 0, __v)	\
	imsic_swap_switchcase_2(__ireg + 2, __v)
#define imsic_swap_switchcase_8(__ireg, __v)		\
	imsic_swap_switchcase_4(__ireg + 0, __v)	\
	imsic_swap_switchcase_4(__ireg + 4, __v)
#define imsic_swap_switchcase_16(__ireg, __v)		\
	imsic_swap_switchcase_8(__ireg + 0, __v)	\
	imsic_swap_switchcase_8(__ireg + 8, __v)
#define imsic_swap_switchcase_32(__ireg, __v)		\
	imsic_swap_switchcase_16(__ireg + 0, __v)	\
	imsic_swap_switchcase_16(__ireg + 16, __v)
#define imsic_swap_switchcase_64(__ireg, __v)		\
	imsic_swap_switchcase_32(__ireg + 0, __v)	\
	imsic_swap_switchcase_32(__ireg + 32, __v)

static unsigned long imsic_eix_swap(int ireg, unsigned long val)
{
	switch (ireg) {
	imsic_swap_switchcase_64(IMSIC_EIP0, val)
	imsic_swap_switchcase_64(IMSIC_EIE0, val)
	}

	return 0;
}

#define imsic_vs_csr_write(__c, __v)		\
do {						\
	csr_write(CSR_VSISELECT, __c);		\
	csr_write(CSR_VSIREG, __v);		\
} while (0)

#define imsic_write_switchcase(__ireg, __v)		\
	case __ireg:					\
		imsic_vs_csr_write(__ireg, __v);	\
		break;
#define imsic_write_switchcase_2(__ireg, __v)		\
	imsic_write_switchcase(__ireg + 0, __v)		\
	imsic_write_switchcase(__ireg + 1, __v)
#define imsic_write_switchcase_4(__ireg, __v)		\
	imsic_write_switchcase_2(__ireg + 0, __v)	\
	imsic_write_switchcase_2(__ireg + 2, __v)
#define imsic_write_switchcase_8(__ireg, __v)		\
	imsic_write_switchcase_4(__ireg + 0, __v)	\
	imsic_write_switchcase_4(__ireg + 4, __v)
#define imsic_write_switchcase_16(__ireg, __v)		\
	imsic_write_switchcase_8(__ireg + 0, __v)	\
	imsic_write_switchcase_8(__ireg + 8, __v)
#define imsic_write_switchcase_32(__ireg, __v)		\
	imsic_write_switchcase_16(__ireg + 0, __v)	\
	imsic_write_switchcase_16(__ireg + 16, __v)
#define imsic_write_switchcase_64(__ireg, __v)		\
	imsic_write_switchcase_32(__ireg + 0, __v)	\
	imsic_write_switchcase_32(__ireg + 32, __v)

static void imsic_eix_write(int ireg, unsigned long val)
{
	switch (ireg) {
	imsic_write_switchcase_64(IMSIC_EIP0, val)
	imsic_write_switchcase_64(IMSIC_EIE0, val)
	}
}

#define imsic_vs_csr_set(__c, __v)		\
do {						\
	csr_write(CSR_VSISELECT, __c);		\
	csr_set(CSR_VSIREG, __v);		\
} while (0)

#define imsic_set_switchcase(__ireg, __v)		\
	case __ireg:					\
		imsic_vs_csr_set(__ireg, __v);		\
		break;
#define imsic_set_switchcase_2(__ireg, __v)		\
	imsic_set_switchcase(__ireg + 0, __v)		\
	imsic_set_switchcase(__ireg + 1, __v)
#define imsic_set_switchcase_4(__ireg, __v)		\
	imsic_set_switchcase_2(__ireg + 0, __v)		\
	imsic_set_switchcase_2(__ireg + 2, __v)
#define imsic_set_switchcase_8(__ireg, __v)		\
	imsic_set_switchcase_4(__ireg + 0, __v)		\
	imsic_set_switchcase_4(__ireg + 4, __v)
#define imsic_set_switchcase_16(__ireg, __v)		\
	imsic_set_switchcase_8(__ireg + 0, __v)		\
	imsic_set_switchcase_8(__ireg + 8, __v)
#define imsic_set_switchcase_32(__ireg, __v)		\
	imsic_set_switchcase_16(__ireg + 0, __v)	\
	imsic_set_switchcase_16(__ireg + 16, __v)
#define imsic_set_switchcase_64(__ireg, __v)		\
	imsic_set_switchcase_32(__ireg + 0, __v)	\
	imsic_set_switchcase_32(__ireg + 32, __v)

static void imsic_eix_set(int ireg, unsigned long val)
{
	switch (ireg) {
	imsic_set_switchcase_64(IMSIC_EIP0, val)
	imsic_set_switchcase_64(IMSIC_EIE0, val)
	}
}

static unsigned long imsic_mrif_atomic_rmw(struct imsic_mrif *mrif,
					   unsigned long *ptr,
					   unsigned long new_val,
					   unsigned long wr_mask)
{
	unsigned long old_val = 0, tmp = 0;

	__asm__ __volatile__ (
		"0:	lr.w.aq   %1, %0\n"
		"	and       %2, %1, %3\n"
		"	or        %2, %2, %4\n"
		"	sc.w.rl   %2, %2, %0\n"
		"	bnez      %2, 0b"
		: "+A" (*ptr), "+r" (old_val), "+r" (tmp)
		: "r" (~wr_mask), "r" (new_val & wr_mask)
		: "memory");

	return old_val;
}

static unsigned long imsic_mrif_atomic_or(struct imsic_mrif *mrif,
					  unsigned long *ptr,
					  unsigned long val)
{
	return atomic_long_fetch_or(val, (atomic_long_t *)ptr);
}

#define imsic_mrif_atomic_write(__mrif, __ptr, __new_val)	\
		imsic_mrif_atomic_rmw(__mrif, __ptr, __new_val, -1UL)
#define imsic_mrif_atomic_read(__mrif, __ptr)			\
		imsic_mrif_atomic_or(__mrif, __ptr, 0)

static u32 imsic_mrif_topei(struct imsic_mrif *mrif, u32 nr_eix, u32 nr_msis)
{
	struct imsic_mrif_eix *eix;
	u32 i, imin, imax, ei, max_msi;
	unsigned long eipend[BITS_PER_TYPE(u64) / BITS_PER_LONG];
	unsigned long eithreshold = imsic_mrif_atomic_read(mrif,
							&mrif->eithreshold);

	max_msi = (eithreshold && (eithreshold <= nr_msis)) ?
		   eithreshold : nr_msis;
	for (ei = 0; ei < nr_eix; ei++) {
		eix = &mrif->eix[ei];
		eipend[0] = imsic_mrif_atomic_read(mrif, &eix->eie[0]) &
			    imsic_mrif_atomic_read(mrif, &eix->eip[0]);
#ifdef CONFIG_32BIT
		eipend[1] = imsic_mrif_atomic_read(mrif, &eix->eie[1]) &
			    imsic_mrif_atomic_read(mrif, &eix->eip[1]);
		if (!eipend[0] && !eipend[1])
#else
		if (!eipend[0])
#endif
			continue;

		imin = ei * BITS_PER_TYPE(u64);
		imax = ((imin + BITS_PER_TYPE(u64)) < max_msi) ?
			imin + BITS_PER_TYPE(u64) : max_msi;
		for (i = (!imin) ? 1 : imin; i < imax; i++) {
			if (test_bit(i - imin, eipend))
				return (i << TOPEI_ID_SHIFT) | i;
		}
	}

	return 0;
}

static int imsic_mrif_isel_check(u32 nr_eix, unsigned long isel)
{
	u32 num = 0;

	switch (isel) {
	case IMSIC_EIDELIVERY:
	case IMSIC_EITHRESHOLD:
		break;
	case IMSIC_EIP0 ... IMSIC_EIP63:
		num = isel - IMSIC_EIP0;
		break;
	case IMSIC_EIE0 ... IMSIC_EIE63:
		num = isel - IMSIC_EIE0;
		break;
	default:
		return -ENOENT;
	}
#ifndef CONFIG_32BIT
	if (num & 0x1)
		return -EINVAL;
#endif
	if ((num / 2) >= nr_eix)
		return -EINVAL;

	return 0;
}

static int imsic_mrif_rmw(struct imsic_mrif *mrif, u32 nr_eix,
			  unsigned long isel, unsigned long *val,
			  unsigned long new_val, unsigned long wr_mask)
{
	bool pend;
	struct imsic_mrif_eix *eix;
	unsigned long *ei, num, old_val = 0;

	switch (isel) {
	case IMSIC_EIDELIVERY:
		old_val = imsic_mrif_atomic_rmw(mrif, &mrif->eidelivery,
						new_val, wr_mask & 0x1);
		break;
	case IMSIC_EITHRESHOLD:
		old_val = imsic_mrif_atomic_rmw(mrif, &mrif->eithreshold,
				new_val, wr_mask & (IMSIC_MAX_ID - 1));
		break;
	case IMSIC_EIP0 ... IMSIC_EIP63:
	case IMSIC_EIE0 ... IMSIC_EIE63:
		if (isel >= IMSIC_EIP0 && isel <= IMSIC_EIP63) {
			pend = true;
			num = isel - IMSIC_EIP0;
		} else {
			pend = false;
			num = isel - IMSIC_EIE0;
		}

		if ((num / 2) >= nr_eix)
			return -EINVAL;
		eix = &mrif->eix[num / 2];

#ifndef CONFIG_32BIT
		if (num & 0x1)
			return -EINVAL;
		ei = (pend) ? &eix->eip[0] : &eix->eie[0];
#else
		ei = (pend) ? &eix->eip[num & 0x1] : &eix->eie[num & 0x1];
#endif

		/* Bit0 of EIP0 or EIE0 is read-only */
		if (!num)
			wr_mask &= ~BIT(0);

		old_val = imsic_mrif_atomic_rmw(mrif, ei, new_val, wr_mask);
		break;
	default:
		return -ENOENT;
	}

	if (val)
		*val = old_val;

	return 0;
}

struct imsic_vsfile_read_data {
	int hgei;
	u32 nr_eix;
	bool clear;
	struct imsic_mrif *mrif;
};

static void imsic_vsfile_local_read(void *data)
{
	u32 i;
	struct imsic_mrif_eix *eix;
	struct imsic_vsfile_read_data *idata = data;
	struct imsic_mrif *mrif = idata->mrif;
	unsigned long new_hstatus, old_hstatus, old_vsiselect;

	old_vsiselect = csr_read(CSR_VSISELECT);
	old_hstatus = csr_read(CSR_HSTATUS);
	new_hstatus = old_hstatus & ~HSTATUS_VGEIN;
	new_hstatus |= ((unsigned long)idata->hgei) << HSTATUS_VGEIN_SHIFT;
	csr_write(CSR_HSTATUS, new_hstatus);

	/*
	 * We don't use imsic_mrif_atomic_xyz() functions to store
	 * values in MRIF because imsic_vsfile_read() is always called
	 * with pointer to temporary MRIF on stack.
	 */

	if (idata->clear) {
		mrif->eidelivery = imsic_vs_csr_swap(IMSIC_EIDELIVERY, 0);
		mrif->eithreshold = imsic_vs_csr_swap(IMSIC_EITHRESHOLD, 0);
		for (i = 0; i < idata->nr_eix; i++) {
			eix = &mrif->eix[i];
			eix->eip[0] = imsic_eix_swap(IMSIC_EIP0 + i * 2, 0);
			eix->eie[0] = imsic_eix_swap(IMSIC_EIE0 + i * 2, 0);
#ifdef CONFIG_32BIT
			eix->eip[1] = imsic_eix_swap(IMSIC_EIP0 + i * 2 + 1, 0);
			eix->eie[1] = imsic_eix_swap(IMSIC_EIE0 + i * 2 + 1, 0);
#endif
		}
	} else {
		mrif->eidelivery = imsic_vs_csr_read(IMSIC_EIDELIVERY);
		mrif->eithreshold = imsic_vs_csr_read(IMSIC_EITHRESHOLD);
		for (i = 0; i < idata->nr_eix; i++) {
			eix = &mrif->eix[i];
			eix->eip[0] = imsic_eix_read(IMSIC_EIP0 + i * 2);
			eix->eie[0] = imsic_eix_read(IMSIC_EIE0 + i * 2);
#ifdef CONFIG_32BIT
			eix->eip[1] = imsic_eix_read(IMSIC_EIP0 + i * 2 + 1);
			eix->eie[1] = imsic_eix_read(IMSIC_EIE0 + i * 2 + 1);
#endif
		}
	}

	csr_write(CSR_HSTATUS, old_hstatus);
	csr_write(CSR_VSISELECT, old_vsiselect);
}

static void imsic_vsfile_read(int vsfile_hgei, int vsfile_cpu, u32 nr_eix,
			      bool clear, struct imsic_mrif *mrif)
{
	struct imsic_vsfile_read_data idata;

	/* We can only read clear if we have a IMSIC VS-file */
	if (vsfile_cpu < 0 || vsfile_hgei <= 0)
		return;

	/* We can only read clear on local CPU */
	idata.hgei = vsfile_hgei;
	idata.nr_eix = nr_eix;
	idata.clear = clear;
	idata.mrif = mrif;
	on_each_cpu_mask(cpumask_of(vsfile_cpu),
			 imsic_vsfile_local_read, &idata, 1);
}

struct imsic_vsfile_rw_data {
	int hgei;
	int isel;
	bool write;
	unsigned long val;
};

static void imsic_vsfile_local_rw(void *data)
{
	struct imsic_vsfile_rw_data *idata = data;
	unsigned long new_hstatus, old_hstatus, old_vsiselect;

	old_vsiselect = csr_read(CSR_VSISELECT);
	old_hstatus = csr_read(CSR_HSTATUS);
	new_hstatus = old_hstatus & ~HSTATUS_VGEIN;
	new_hstatus |= ((unsigned long)idata->hgei) << HSTATUS_VGEIN_SHIFT;
	csr_write(CSR_HSTATUS, new_hstatus);

	switch (idata->isel) {
	case IMSIC_EIDELIVERY:
		if (idata->write)
			imsic_vs_csr_write(IMSIC_EIDELIVERY, idata->val);
		else
			idata->val = imsic_vs_csr_read(IMSIC_EIDELIVERY);
		break;
	case IMSIC_EITHRESHOLD:
		if (idata->write)
			imsic_vs_csr_write(IMSIC_EITHRESHOLD, idata->val);
		else
			idata->val = imsic_vs_csr_read(IMSIC_EITHRESHOLD);
		break;
	case IMSIC_EIP0 ... IMSIC_EIP63:
	case IMSIC_EIE0 ... IMSIC_EIE63:
#ifndef CONFIG_32BIT
		if (idata->isel & 0x1)
			break;
#endif
		if (idata->write)
			imsic_eix_write(idata->isel, idata->val);
		else
			idata->val = imsic_eix_read(idata->isel);
		break;
	default:
		break;
	}

	csr_write(CSR_HSTATUS, old_hstatus);
	csr_write(CSR_VSISELECT, old_vsiselect);
}

static int imsic_vsfile_rw(int vsfile_hgei, int vsfile_cpu, u32 nr_eix,
			   unsigned long isel, bool write,
			   unsigned long *val)
{
	int rc;
	struct imsic_vsfile_rw_data rdata;

	/* We can only access register if we have a IMSIC VS-file */
	if (vsfile_cpu < 0 || vsfile_hgei <= 0)
		return -EINVAL;

	/* Check IMSIC register iselect */
	rc = imsic_mrif_isel_check(nr_eix, isel);
	if (rc)
		return rc;

	/* We can only access register on local CPU */
	rdata.hgei = vsfile_hgei;
	rdata.isel = isel;
	rdata.write = write;
	rdata.val = (write) ? *val : 0;
	on_each_cpu_mask(cpumask_of(vsfile_cpu),
			 imsic_vsfile_local_rw, &rdata, 1);

	if (!write)
		*val = rdata.val;

	return 0;
}

static void imsic_vsfile_local_clear(int vsfile_hgei, u32 nr_eix)
{
	u32 i;
	unsigned long new_hstatus, old_hstatus, old_vsiselect;

	/* We can only zero-out if we have a IMSIC VS-file */
	if (vsfile_hgei <= 0)
		return;

	old_vsiselect = csr_read(CSR_VSISELECT);
	old_hstatus = csr_read(CSR_HSTATUS);
	new_hstatus = old_hstatus & ~HSTATUS_VGEIN;
	new_hstatus |= ((unsigned long)vsfile_hgei) << HSTATUS_VGEIN_SHIFT;
	csr_write(CSR_HSTATUS, new_hstatus);

	imsic_vs_csr_write(IMSIC_EIDELIVERY, 0);
	imsic_vs_csr_write(IMSIC_EITHRESHOLD, 0);
	for (i = 0; i < nr_eix; i++) {
		imsic_eix_write(IMSIC_EIP0 + i * 2, 0);
		imsic_eix_write(IMSIC_EIE0 + i * 2, 0);
#ifdef CONFIG_32BIT
		imsic_eix_write(IMSIC_EIP0 + i * 2 + 1, 0);
		imsic_eix_write(IMSIC_EIE0 + i * 2 + 1, 0);
#endif
	}

	csr_write(CSR_HSTATUS, old_hstatus);
	csr_write(CSR_VSISELECT, old_vsiselect);
}

static void imsic_vsfile_local_update(int vsfile_hgei, u32 nr_eix,
				      struct imsic_mrif *mrif)
{
	u32 i;
	struct imsic_mrif_eix *eix;
	unsigned long new_hstatus, old_hstatus, old_vsiselect;

	/* We can only update if we have a HW IMSIC context */
	if (vsfile_hgei <= 0)
		return;

	/*
	 * We don't use imsic_mrif_atomic_xyz() functions to read values
	 * from MRIF in this function because it is always called with
	 * pointer to temporary MRIF on stack.
	 */

	old_vsiselect = csr_read(CSR_VSISELECT);
	old_hstatus = csr_read(CSR_HSTATUS);
	new_hstatus = old_hstatus & ~HSTATUS_VGEIN;
	new_hstatus |= ((unsigned long)vsfile_hgei) << HSTATUS_VGEIN_SHIFT;
	csr_write(CSR_HSTATUS, new_hstatus);

	for (i = 0; i < nr_eix; i++) {
		eix = &mrif->eix[i];
		imsic_eix_set(IMSIC_EIP0 + i * 2, eix->eip[0]);
		imsic_eix_set(IMSIC_EIE0 + i * 2, eix->eie[0]);
#ifdef CONFIG_32BIT
		imsic_eix_set(IMSIC_EIP0 + i * 2 + 1, eix->eip[1]);
		imsic_eix_set(IMSIC_EIE0 + i * 2 + 1, eix->eie[1]);
#endif
	}
	imsic_vs_csr_write(IMSIC_EITHRESHOLD, mrif->eithreshold);
	imsic_vs_csr_write(IMSIC_EIDELIVERY, mrif->eidelivery);

	csr_write(CSR_HSTATUS, old_hstatus);
	csr_write(CSR_VSISELECT, old_vsiselect);
}

static void imsic_vsfile_cleanup(struct imsic *imsic)
{
	int old_vsfile_hgei, old_vsfile_cpu;
	unsigned long flags;

	/*
	 * We don't use imsic_mrif_atomic_xyz() functions to clear the
	 * SW-file in this function because it is always called when the
	 * VCPU is being destroyed.
	 */

	write_lock_irqsave(&imsic->vsfile_lock, flags);
	old_vsfile_hgei = imsic->vsfile_hgei;
	old_vsfile_cpu = imsic->vsfile_cpu;
	imsic->vsfile_cpu = imsic->vsfile_hgei = -1;
	imsic->vsfile_va = NULL;
	imsic->vsfile_pa = 0;
	write_unlock_irqrestore(&imsic->vsfile_lock, flags);

	memset(imsic->swfile, 0, sizeof(*imsic->swfile));

	if (old_vsfile_cpu >= 0)
		kvm_riscv_aia_free_hgei(old_vsfile_cpu, old_vsfile_hgei);
}

static void imsic_swfile_extirq_update(struct kvm_vcpu *vcpu)
{
	struct imsic *imsic = vcpu->arch.aia_context.imsic_state;
	struct imsic_mrif *mrif = imsic->swfile;
	unsigned long flags;

	/*
	 * The critical section is necessary during external interrupt
	 * updates to avoid the risk of losing interrupts due to potential
	 * interruptions between reading topei and updating pending status.
	 */

	raw_spin_lock_irqsave(&imsic->swfile_extirq_lock, flags);

	if (imsic_mrif_atomic_read(mrif, &mrif->eidelivery) &&
	    imsic_mrif_topei(mrif, imsic->nr_eix, imsic->nr_msis))
		kvm_riscv_vcpu_set_interrupt(vcpu, IRQ_VS_EXT);
	else
		kvm_riscv_vcpu_unset_interrupt(vcpu, IRQ_VS_EXT);

	raw_spin_unlock_irqrestore(&imsic->swfile_extirq_lock, flags);
}

static void imsic_swfile_read(struct kvm_vcpu *vcpu, bool clear,
			      struct imsic_mrif *mrif)
{
	struct imsic *imsic = vcpu->arch.aia_context.imsic_state;

	/*
	 * We don't use imsic_mrif_atomic_xyz() functions to read and
	 * write SW-file and MRIF in this function because it is always
	 * called when VCPU is not using SW-file and the MRIF points to
	 * a temporary MRIF on stack.
	 */

	memcpy(mrif, imsic->swfile, sizeof(*mrif));
	if (clear) {
		memset(imsic->swfile, 0, sizeof(*imsic->swfile));
		kvm_riscv_vcpu_unset_interrupt(vcpu, IRQ_VS_EXT);
	}
}

static void imsic_swfile_update(struct kvm_vcpu *vcpu,
				struct imsic_mrif *mrif)
{
	u32 i;
	struct imsic_mrif_eix *seix, *eix;
	struct imsic *imsic = vcpu->arch.aia_context.imsic_state;
	struct imsic_mrif *smrif = imsic->swfile;

	imsic_mrif_atomic_write(smrif, &smrif->eidelivery, mrif->eidelivery);
	imsic_mrif_atomic_write(smrif, &smrif->eithreshold, mrif->eithreshold);
	for (i = 0; i < imsic->nr_eix; i++) {
		seix = &smrif->eix[i];
		eix = &mrif->eix[i];
		imsic_mrif_atomic_or(smrif, &seix->eip[0], eix->eip[0]);
		imsic_mrif_atomic_or(smrif, &seix->eie[0], eix->eie[0]);
#ifdef CONFIG_32BIT
		imsic_mrif_atomic_or(smrif, &seix->eip[1], eix->eip[1]);
		imsic_mrif_atomic_or(smrif, &seix->eie[1], eix->eie[1]);
#endif
	}

	imsic_swfile_extirq_update(vcpu);
}

void kvm_riscv_vcpu_aia_imsic_release(struct kvm_vcpu *vcpu)
{
	unsigned long flags;
	struct imsic_mrif tmrif;
	int old_vsfile_hgei, old_vsfile_cpu;
	struct imsic *imsic = vcpu->arch.aia_context.imsic_state;

	/* Read and clear IMSIC VS-file details */
	write_lock_irqsave(&imsic->vsfile_lock, flags);
	old_vsfile_hgei = imsic->vsfile_hgei;
	old_vsfile_cpu = imsic->vsfile_cpu;
	imsic->vsfile_cpu = imsic->vsfile_hgei = -1;
	imsic->vsfile_va = NULL;
	imsic->vsfile_pa = 0;
	write_unlock_irqrestore(&imsic->vsfile_lock, flags);

	/* Do nothing, if no IMSIC VS-file to release */
	if (old_vsfile_cpu < 0)
		return;

	/*
	 * At this point, all interrupt producers are still using
	 * the old IMSIC VS-file so we first re-direct all interrupt
	 * producers.
	 */

	/* Purge the G-stage mapping */
	kvm_riscv_gstage_iounmap(vcpu->kvm,
				 vcpu->arch.aia_context.imsic_addr,
				 IMSIC_MMIO_PAGE_SZ);

	/* TODO: Purge the IOMMU mapping ??? */

	/*
	 * At this point, all interrupt producers have been re-directed
	 * to somewhere else so we move register state from the old IMSIC
	 * VS-file to the IMSIC SW-file.
	 */

	/* Read and clear register state from old IMSIC VS-file */
	memset(&tmrif, 0, sizeof(tmrif));
	imsic_vsfile_read(old_vsfile_hgei, old_vsfile_cpu, imsic->nr_hw_eix,
			  true, &tmrif);

	/* Update register state in IMSIC SW-file */
	imsic_swfile_update(vcpu, &tmrif);

	/* Free-up old IMSIC VS-file */
	kvm_riscv_aia_free_hgei(old_vsfile_cpu, old_vsfile_hgei);
}

int kvm_riscv_vcpu_aia_imsic_update(struct kvm_vcpu *vcpu)
{
	unsigned long flags;
	phys_addr_t new_vsfile_pa;
	struct imsic_mrif tmrif;
	void __iomem *new_vsfile_va;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_run *run = vcpu->run;
	struct kvm_vcpu_aia *vaia = &vcpu->arch.aia_context;
	struct imsic *imsic = vaia->imsic_state;
	int ret = 0, new_vsfile_hgei = -1, old_vsfile_hgei, old_vsfile_cpu;

	/* Do nothing for emulation mode */
	if (kvm->arch.aia.mode == KVM_DEV_RISCV_AIA_MODE_EMUL)
		return 1;

	/* Read old IMSIC VS-file details */
	read_lock_irqsave(&imsic->vsfile_lock, flags);
	old_vsfile_hgei = imsic->vsfile_hgei;
	old_vsfile_cpu = imsic->vsfile_cpu;
	read_unlock_irqrestore(&imsic->vsfile_lock, flags);

	/* Do nothing if we are continuing on same CPU */
	if (old_vsfile_cpu == vcpu->cpu)
		return 1;

	/* Allocate new IMSIC VS-file */
	ret = kvm_riscv_aia_alloc_hgei(vcpu->cpu, vcpu,
				       &new_vsfile_va, &new_vsfile_pa);
	if (ret <= 0) {
		/* For HW acceleration mode, we can't continue */
		if (kvm->arch.aia.mode == KVM_DEV_RISCV_AIA_MODE_HWACCEL) {
			run->fail_entry.hardware_entry_failure_reason =
								CSR_HSTATUS;
			run->fail_entry.cpu = vcpu->cpu;
			run->exit_reason = KVM_EXIT_FAIL_ENTRY;
			return 0;
		}

		/* Release old IMSIC VS-file */
		if (old_vsfile_cpu >= 0)
			kvm_riscv_vcpu_aia_imsic_release(vcpu);

		/* For automatic mode, we continue */
		goto done;
	}
	new_vsfile_hgei = ret;

	/*
	 * At this point, all interrupt producers are still using
	 * to the old IMSIC VS-file so we first move all interrupt
	 * producers to the new IMSIC VS-file.
	 */

	/* Zero-out new IMSIC VS-file */
	imsic_vsfile_local_clear(new_vsfile_hgei, imsic->nr_hw_eix);

	/* Update G-stage mapping for the new IMSIC VS-file */
	ret = kvm_riscv_gstage_ioremap(kvm, vcpu->arch.aia_context.imsic_addr,
				       new_vsfile_pa, IMSIC_MMIO_PAGE_SZ,
				       true, true);
	if (ret)
		goto fail_free_vsfile_hgei;

	/* TODO: Update the IOMMU mapping ??? */

	/* Update new IMSIC VS-file details in IMSIC context */
	write_lock_irqsave(&imsic->vsfile_lock, flags);
	imsic->vsfile_hgei = new_vsfile_hgei;
	imsic->vsfile_cpu = vcpu->cpu;
	imsic->vsfile_va = new_vsfile_va;
	imsic->vsfile_pa = new_vsfile_pa;
	write_unlock_irqrestore(&imsic->vsfile_lock, flags);

	/*
	 * At this point, all interrupt producers have been moved
	 * to the new IMSIC VS-file so we move register state from
	 * the old IMSIC VS/SW-file to the new IMSIC VS-file.
	 */

	memset(&tmrif, 0, sizeof(tmrif));
	if (old_vsfile_cpu >= 0) {
		/* Read and clear register state from old IMSIC VS-file */
		imsic_vsfile_read(old_vsfile_hgei, old_vsfile_cpu,
				  imsic->nr_hw_eix, true, &tmrif);

		/* Free-up old IMSIC VS-file */
		kvm_riscv_aia_free_hgei(old_vsfile_cpu, old_vsfile_hgei);
	} else {
		/* Read and clear register state from IMSIC SW-file */
		imsic_swfile_read(vcpu, true, &tmrif);
	}

	/* Restore register state in the new IMSIC VS-file */
	imsic_vsfile_local_update(new_vsfile_hgei, imsic->nr_hw_eix, &tmrif);

done:
	/* Set VCPU HSTATUS.VGEIN to new IMSIC VS-file */
	vcpu->arch.guest_context.hstatus &= ~HSTATUS_VGEIN;
	if (new_vsfile_hgei > 0)
		vcpu->arch.guest_context.hstatus |=
			((unsigned long)new_vsfile_hgei) << HSTATUS_VGEIN_SHIFT;

	/* Continue run-loop */
	return 1;

fail_free_vsfile_hgei:
	kvm_riscv_aia_free_hgei(vcpu->cpu, new_vsfile_hgei);
	return ret;
}

int kvm_riscv_vcpu_aia_imsic_rmw(struct kvm_vcpu *vcpu, unsigned long isel,
				 unsigned long *val, unsigned long new_val,
				 unsigned long wr_mask)
{
	u32 topei;
	struct imsic_mrif_eix *eix;
	int r, rc = KVM_INSN_CONTINUE_NEXT_SEPC;
	struct imsic *imsic = vcpu->arch.aia_context.imsic_state;

	if (isel == KVM_RISCV_AIA_IMSIC_TOPEI) {
		/* Read pending and enabled interrupt with highest priority */
		topei = imsic_mrif_topei(imsic->swfile, imsic->nr_eix,
					 imsic->nr_msis);
		if (val)
			*val = topei;

		/* Writes ignore value and clear top pending interrupt */
		if (topei && wr_mask) {
			topei >>= TOPEI_ID_SHIFT;
			if (topei) {
				eix = &imsic->swfile->eix[topei /
							  BITS_PER_TYPE(u64)];
				clear_bit(topei & (BITS_PER_TYPE(u64) - 1),
					  eix->eip);
			}
		}
	} else {
		r = imsic_mrif_rmw(imsic->swfile, imsic->nr_eix, isel,
				   val, new_val, wr_mask);
		/* Forward unknown IMSIC register to user-space */
		if (r)
			rc = (r == -ENOENT) ? 0 : KVM_INSN_ILLEGAL_TRAP;
	}

	if (wr_mask)
		imsic_swfile_extirq_update(vcpu);

	return rc;
}

int kvm_riscv_aia_imsic_rw_attr(struct kvm *kvm, unsigned long type,
				bool write, unsigned long *val)
{
	u32 isel, vcpu_id;
	unsigned long flags;
	struct imsic *imsic;
	struct kvm_vcpu *vcpu;
	int rc, vsfile_hgei, vsfile_cpu;

	if (!kvm_riscv_aia_initialized(kvm))
		return -ENODEV;

	vcpu_id = KVM_DEV_RISCV_AIA_IMSIC_GET_VCPU(type);
	vcpu = kvm_get_vcpu_by_id(kvm, vcpu_id);
	if (!vcpu)
		return -ENODEV;

	isel = KVM_DEV_RISCV_AIA_IMSIC_GET_ISEL(type);
	imsic = vcpu->arch.aia_context.imsic_state;

	read_lock_irqsave(&imsic->vsfile_lock, flags);

	rc = 0;
	vsfile_hgei = imsic->vsfile_hgei;
	vsfile_cpu = imsic->vsfile_cpu;
	if (vsfile_cpu < 0) {
		if (write) {
			rc = imsic_mrif_rmw(imsic->swfile, imsic->nr_eix,
					    isel, NULL, *val, -1UL);
			imsic_swfile_extirq_update(vcpu);
		} else
			rc = imsic_mrif_rmw(imsic->swfile, imsic->nr_eix,
					    isel, val, 0, 0);
	}

	read_unlock_irqrestore(&imsic->vsfile_lock, flags);

	if (!rc && vsfile_cpu >= 0)
		rc = imsic_vsfile_rw(vsfile_hgei, vsfile_cpu, imsic->nr_eix,
				     isel, write, val);

	return rc;
}

int kvm_riscv_aia_imsic_has_attr(struct kvm *kvm, unsigned long type)
{
	u32 isel, vcpu_id;
	struct imsic *imsic;
	struct kvm_vcpu *vcpu;

	if (!kvm_riscv_aia_initialized(kvm))
		return -ENODEV;

	vcpu_id = KVM_DEV_RISCV_AIA_IMSIC_GET_VCPU(type);
	vcpu = kvm_get_vcpu_by_id(kvm, vcpu_id);
	if (!vcpu)
		return -ENODEV;

	isel = KVM_DEV_RISCV_AIA_IMSIC_GET_ISEL(type);
	imsic = vcpu->arch.aia_context.imsic_state;
	return imsic_mrif_isel_check(imsic->nr_eix, isel);
}

void kvm_riscv_vcpu_aia_imsic_reset(struct kvm_vcpu *vcpu)
{
	struct imsic *imsic = vcpu->arch.aia_context.imsic_state;

	if (!imsic)
		return;

	kvm_riscv_vcpu_aia_imsic_release(vcpu);

	memset(imsic->swfile, 0, sizeof(*imsic->swfile));
}

int kvm_riscv_vcpu_aia_imsic_inject(struct kvm_vcpu *vcpu,
				    u32 guest_index, u32 offset, u32 iid)
{
	unsigned long flags;
	struct imsic_mrif_eix *eix;
	struct imsic *imsic = vcpu->arch.aia_context.imsic_state;

	/* We only emulate one IMSIC MMIO page for each Guest VCPU */
	if (!imsic || !iid || guest_index ||
	    (offset != IMSIC_MMIO_SETIPNUM_LE &&
	     offset != IMSIC_MMIO_SETIPNUM_BE))
		return -ENODEV;

	iid = (offset == IMSIC_MMIO_SETIPNUM_BE) ? __swab32(iid) : iid;
	if (imsic->nr_msis <= iid)
		return -EINVAL;

	read_lock_irqsave(&imsic->vsfile_lock, flags);

	if (imsic->vsfile_cpu >= 0) {
		writel(iid, imsic->vsfile_va + IMSIC_MMIO_SETIPNUM_LE);
	} else {
		eix = &imsic->swfile->eix[iid / BITS_PER_TYPE(u64)];
		set_bit(iid & (BITS_PER_TYPE(u64) - 1), eix->eip);
		imsic_swfile_extirq_update(vcpu);
	}

	read_unlock_irqrestore(&imsic->vsfile_lock, flags);

	return 0;
}

static int imsic_mmio_read(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			   gpa_t addr, int len, void *val)
{
	if (len != 4 || (addr & 0x3) != 0)
		return -EOPNOTSUPP;

	*((u32 *)val) = 0;

	return 0;
}

static int imsic_mmio_write(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			    gpa_t addr, int len, const void *val)
{
	struct kvm_msi msi = { 0 };

	if (len != 4 || (addr & 0x3) != 0)
		return -EOPNOTSUPP;

	msi.address_hi = addr >> 32;
	msi.address_lo = (u32)addr;
	msi.data = *((const u32 *)val);
	kvm_riscv_aia_inject_msi(vcpu->kvm, &msi);

	return 0;
};

static struct kvm_io_device_ops imsic_iodoev_ops = {
	.read = imsic_mmio_read,
	.write = imsic_mmio_write,
};

int kvm_riscv_vcpu_aia_imsic_init(struct kvm_vcpu *vcpu)
{
	int ret = 0;
	struct imsic *imsic;
	struct page *swfile_page;
	struct kvm *kvm = vcpu->kvm;

	/* Fail if we have zero IDs */
	if (!kvm->arch.aia.nr_ids)
		return -EINVAL;

	/* Allocate IMSIC context */
	imsic = kzalloc(sizeof(*imsic), GFP_KERNEL);
	if (!imsic)
		return -ENOMEM;
	vcpu->arch.aia_context.imsic_state = imsic;

	/* Setup IMSIC context  */
	imsic->nr_msis = kvm->arch.aia.nr_ids + 1;
	rwlock_init(&imsic->vsfile_lock);
	imsic->nr_eix = BITS_TO_U64(imsic->nr_msis);
	imsic->nr_hw_eix = BITS_TO_U64(kvm_riscv_aia_max_ids);
	imsic->vsfile_hgei = imsic->vsfile_cpu = -1;

	/* Setup IMSIC SW-file */
	swfile_page = alloc_pages(GFP_KERNEL | __GFP_ZERO,
				  get_order(sizeof(*imsic->swfile)));
	if (!swfile_page) {
		ret = -ENOMEM;
		goto fail_free_imsic;
	}
	imsic->swfile = page_to_virt(swfile_page);
	imsic->swfile_pa = page_to_phys(swfile_page);
	raw_spin_lock_init(&imsic->swfile_extirq_lock);

	/* Setup IO device */
	kvm_iodevice_init(&imsic->iodev, &imsic_iodoev_ops);
	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS,
				      vcpu->arch.aia_context.imsic_addr,
				      KVM_DEV_RISCV_IMSIC_SIZE,
				      &imsic->iodev);
	mutex_unlock(&kvm->slots_lock);
	if (ret)
		goto fail_free_swfile;

	return 0;

fail_free_swfile:
	free_pages((unsigned long)imsic->swfile,
		   get_order(sizeof(*imsic->swfile)));
fail_free_imsic:
	vcpu->arch.aia_context.imsic_state = NULL;
	kfree(imsic);
	return ret;
}

void kvm_riscv_vcpu_aia_imsic_cleanup(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct imsic *imsic = vcpu->arch.aia_context.imsic_state;

	if (!imsic)
		return;

	imsic_vsfile_cleanup(imsic);

	mutex_lock(&kvm->slots_lock);
	kvm_io_bus_unregister_dev(kvm, KVM_MMIO_BUS, &imsic->iodev);
	mutex_unlock(&kvm->slots_lock);

	free_pages((unsigned long)imsic->swfile,
		   get_order(sizeof(*imsic->swfile)));

	vcpu->arch.aia_context.imsic_state = NULL;
	kfree(imsic);
}
