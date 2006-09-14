/*
 *  linux/drivers/net/ehea/ehea_hw.h
 *
 *  eHEA ethernet device driver for IBM eServer System p
 *
 *  (C) Copyright IBM Corp. 2006
 *
 *  Authors:
 *       Christoph Raisch <raisch@de.ibm.com>
 *       Jan-Bernd Themann <themann@de.ibm.com>
 *       Thomas Klein <tklein@de.ibm.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __EHEA_HW_H__
#define __EHEA_HW_H__

#define QPX_SQA_VALUE   EHEA_BMASK_IBM(48,63)
#define QPX_RQ1A_VALUE  EHEA_BMASK_IBM(48,63)
#define QPX_RQ2A_VALUE  EHEA_BMASK_IBM(48,63)
#define QPX_RQ3A_VALUE  EHEA_BMASK_IBM(48,63)

#define QPTEMM_OFFSET(x) offsetof(struct ehea_qptemm, x)

struct ehea_qptemm {
	u64 qpx_hcr;
	u64 qpx_c;
	u64 qpx_herr;
	u64 qpx_aer;
	u64 qpx_sqa;
	u64 qpx_sqc;
	u64 qpx_rq1a;
	u64 qpx_rq1c;
	u64 qpx_st;
	u64 qpx_aerr;
	u64 qpx_tenure;
	u64 qpx_reserved1[(0x098 - 0x058) / 8];
	u64 qpx_portp;
	u64 qpx_reserved2[(0x100 - 0x0A0) / 8];
	u64 qpx_t;
	u64 qpx_sqhp;
	u64 qpx_sqptp;
	u64 qpx_reserved3[(0x140 - 0x118) / 8];
	u64 qpx_sqwsize;
	u64 qpx_reserved4[(0x170 - 0x148) / 8];
	u64 qpx_sqsize;
	u64 qpx_reserved5[(0x1B0 - 0x178) / 8];
	u64 qpx_sigt;
	u64 qpx_wqecnt;
	u64 qpx_rq1hp;
	u64 qpx_rq1ptp;
	u64 qpx_rq1size;
	u64 qpx_reserved6[(0x220 - 0x1D8) / 8];
	u64 qpx_rq1wsize;
	u64 qpx_reserved7[(0x240 - 0x228) / 8];
	u64 qpx_pd;
	u64 qpx_scqn;
	u64 qpx_rcqn;
	u64 qpx_aeqn;
	u64 reserved49;
	u64 qpx_ram;
	u64 qpx_reserved8[(0x300 - 0x270) / 8];
	u64 qpx_rq2a;
	u64 qpx_rq2c;
	u64 qpx_rq2hp;
	u64 qpx_rq2ptp;
	u64 qpx_rq2size;
	u64 qpx_rq2wsize;
	u64 qpx_rq2th;
	u64 qpx_rq3a;
	u64 qpx_rq3c;
	u64 qpx_rq3hp;
	u64 qpx_rq3ptp;
	u64 qpx_rq3size;
	u64 qpx_rq3wsize;
	u64 qpx_rq3th;
	u64 qpx_lpn;
	u64 qpx_reserved9[(0x400 - 0x378) / 8];
	u64 reserved_ext[(0x500 - 0x400) / 8];
	u64 reserved2[(0x1000 - 0x500) / 8];
};

#define MRx_HCR_LPARID_VALID EHEA_BMASK_IBM(0, 0)

#define MRMWMM_OFFSET(x) offsetof(struct ehea_mrmwmm, x)

struct ehea_mrmwmm {
	u64 mrx_hcr;
	u64 mrx_c;
	u64 mrx_herr;
	u64 mrx_aer;
	u64 mrx_pp;
	u64 reserved1;
	u64 reserved2;
	u64 reserved3;
	u64 reserved4[(0x200 - 0x40) / 8];
	u64 mrx_ctl[64];
};

#define QPEDMM_OFFSET(x) offsetof(struct ehea_qpedmm, x)

struct ehea_qpedmm {

	u64 reserved0[(0x400) / 8];
	u64 qpedx_phh;
	u64 qpedx_ppsgp;
	u64 qpedx_ppsgu;
	u64 qpedx_ppdgp;
	u64 qpedx_ppdgu;
	u64 qpedx_aph;
	u64 qpedx_apsgp;
	u64 qpedx_apsgu;
	u64 qpedx_apdgp;
	u64 qpedx_apdgu;
	u64 qpedx_apav;
	u64 qpedx_apsav;
	u64 qpedx_hcr;
	u64 reserved1[4];
	u64 qpedx_rrl0;
	u64 qpedx_rrrkey0;
	u64 qpedx_rrva0;
	u64 reserved2;
	u64 qpedx_rrl1;
	u64 qpedx_rrrkey1;
	u64 qpedx_rrva1;
	u64 reserved3;
	u64 qpedx_rrl2;
	u64 qpedx_rrrkey2;
	u64 qpedx_rrva2;
	u64 reserved4;
	u64 qpedx_rrl3;
	u64 qpedx_rrrkey3;
	u64 qpedx_rrva3;
};

#define CQX_FECADDER EHEA_BMASK_IBM(32, 63)
#define CQX_FEC_CQE_CNT EHEA_BMASK_IBM(32, 63)
#define CQX_N1_GENERATE_COMP_EVENT EHEA_BMASK_IBM(0, 0)
#define CQX_EP_EVENT_PENDING EHEA_BMASK_IBM(0, 0)

#define CQTEMM_OFFSET(x) offsetof(struct ehea_cqtemm, x)

struct ehea_cqtemm {
	u64 cqx_hcr;
	u64 cqx_c;
	u64 cqx_herr;
	u64 cqx_aer;
	u64 cqx_ptp;
	u64 cqx_tp;
	u64 cqx_fec;
	u64 cqx_feca;
	u64 cqx_ep;
	u64 cqx_eq;
	u64 reserved1;
	u64 cqx_n0;
	u64 cqx_n1;
	u64 reserved2[(0x1000 - 0x60) / 8];
};

#define EQTEMM_OFFSET(x) offsetof(struct ehea_eqtemm, x)

struct ehea_eqtemm {
	u64 eqx_hcr;
	u64 eqx_c;
	u64 eqx_herr;
	u64 eqx_aer;
	u64 eqx_ptp;
	u64 eqx_tp;
	u64 eqx_ssba;
	u64 eqx_psba;
	u64 eqx_cec;
	u64 eqx_meql;
	u64 eqx_xisbi;
	u64 eqx_xisc;
	u64 eqx_it;
};

/*
 * These access functions will be changed when the dissuccsion about
 * the new access methods for POWER has settled.
 */

static inline u64 epa_load(struct h_epa epa, u32 offset)
{
	return __raw_readq((void __iomem *)(epa.addr + offset));
}

static inline void epa_store(struct h_epa epa, u32 offset, u64 value)
{
	__raw_writeq(value, (void __iomem *)(epa.addr + offset));
	epa_load(epa, offset);	/* synchronize explicitly to eHEA */
}

static inline void epa_store_acc(struct h_epa epa, u32 offset, u64 value)
{
	__raw_writeq(value, (void __iomem *)(epa.addr + offset));
}

#define epa_store_eq(epa, offset, value)\
        epa_store(epa, EQTEMM_OFFSET(offset), value)
#define epa_load_eq(epa, offset)\
        epa_load(epa, EQTEMM_OFFSET(offset))

#define epa_store_cq(epa, offset, value)\
        epa_store(epa, CQTEMM_OFFSET(offset), value)
#define epa_load_cq(epa, offset)\
        epa_load(epa, CQTEMM_OFFSET(offset))

#define epa_store_qp(epa, offset, value)\
        epa_store(epa, QPTEMM_OFFSET(offset), value)
#define epa_load_qp(epa, offset)\
        epa_load(epa, QPTEMM_OFFSET(offset))

#define epa_store_qped(epa, offset, value)\
        epa_store(epa, QPEDMM_OFFSET(offset), value)
#define epa_load_qped(epa, offset)\
        epa_load(epa, QPEDMM_OFFSET(offset))

#define epa_store_mrmw(epa, offset, value)\
        epa_store(epa, MRMWMM_OFFSET(offset), value)
#define epa_load_mrmw(epa, offset)\
        epa_load(epa, MRMWMM_OFFSET(offset))

#define epa_store_base(epa, offset, value)\
        epa_store(epa, HCAGR_OFFSET(offset), value)
#define epa_load_base(epa, offset)\
        epa_load(epa, HCAGR_OFFSET(offset))

static inline void ehea_update_sqa(struct ehea_qp *qp, u16 nr_wqes)
{
	struct h_epa epa = qp->epas.kernel;
	epa_store_acc(epa, QPTEMM_OFFSET(qpx_sqa),
		      EHEA_BMASK_SET(QPX_SQA_VALUE, nr_wqes));
}

static inline void ehea_update_rq3a(struct ehea_qp *qp, u16 nr_wqes)
{
	struct h_epa epa = qp->epas.kernel;
	epa_store_acc(epa, QPTEMM_OFFSET(qpx_rq3a),
		      EHEA_BMASK_SET(QPX_RQ1A_VALUE, nr_wqes));
}

static inline void ehea_update_rq2a(struct ehea_qp *qp, u16 nr_wqes)
{
	struct h_epa epa = qp->epas.kernel;
	epa_store_acc(epa, QPTEMM_OFFSET(qpx_rq2a),
		      EHEA_BMASK_SET(QPX_RQ2A_VALUE, nr_wqes));
}

static inline void ehea_update_rq1a(struct ehea_qp *qp, u16 nr_wqes)
{
	struct h_epa epa = qp->epas.kernel;
	epa_store_acc(epa, QPTEMM_OFFSET(qpx_rq1a),
		      EHEA_BMASK_SET(QPX_RQ3A_VALUE, nr_wqes));
}

static inline void ehea_update_feca(struct ehea_cq *cq, u32 nr_cqes)
{
	struct h_epa epa = cq->epas.kernel;
	epa_store_acc(epa, CQTEMM_OFFSET(cqx_feca),
		      EHEA_BMASK_SET(CQX_FECADDER, nr_cqes));
}

static inline void ehea_reset_cq_n1(struct ehea_cq *cq)
{
	struct h_epa epa = cq->epas.kernel;
	epa_store_cq(epa, cqx_n1,
		     EHEA_BMASK_SET(CQX_N1_GENERATE_COMP_EVENT, 1));
}

static inline void ehea_reset_cq_ep(struct ehea_cq *my_cq)
{
	struct h_epa epa = my_cq->epas.kernel;
	epa_store_acc(epa, CQTEMM_OFFSET(cqx_ep),
		      EHEA_BMASK_SET(CQX_EP_EVENT_PENDING, 0));
}

#endif	/* __EHEA_HW_H__ */
