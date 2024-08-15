// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2019 Microsemi Corporation
 */

#include <soc/mscc/ocelot.h>
#include "ocelot_police.h"

/* Types for ANA:POL[0-192]:POL_MODE_CFG.FRM_MODE */
#define POL_MODE_LINERATE   0 /* Incl IPG. Unit: 33 1/3 kbps, 4096 bytes */
#define POL_MODE_DATARATE   1 /* Excl IPG. Unit: 33 1/3 kbps, 4096 bytes  */
#define POL_MODE_FRMRATE_HI 2 /* Unit: 33 1/3 fps, 32.8 frames */
#define POL_MODE_FRMRATE_LO 3 /* Unit: 1/3 fps, 0.3 frames */

/* Policer indexes */
#define POL_IX_PORT    0    /* 0-11    : Port policers */
#define POL_IX_QUEUE   32   /* 32-127  : Queue policers  */

/* Default policer order */
#define POL_ORDER 0x1d3 /* Ocelot policer order: Serial (QoS -> Port -> VCAP) */

int qos_policer_conf_set(struct ocelot *ocelot, u32 pol_ix,
			 struct qos_policer_conf *conf)
{
	u32 cf = 0, cir_ena = 0, frm_mode = POL_MODE_LINERATE;
	u32 cir = 0, cbs = 0, pir = 0, pbs = 0;
	bool cir_discard = 0, pir_discard = 0;
	u32 pbs_max = 0, cbs_max = 0;
	u8 ipg = 20;
	u32 value;

	pir = conf->pir;
	pbs = conf->pbs;

	switch (conf->mode) {
	case MSCC_QOS_RATE_MODE_LINE:
	case MSCC_QOS_RATE_MODE_DATA:
		if (conf->mode == MSCC_QOS_RATE_MODE_LINE) {
			frm_mode = POL_MODE_LINERATE;
			ipg = min_t(u8, GENMASK(4, 0), conf->ipg);
		} else {
			frm_mode = POL_MODE_DATARATE;
		}
		if (conf->dlb) {
			cir_ena = 1;
			cir = conf->cir;
			cbs = conf->cbs;
			if (cir == 0 && cbs == 0) {
				/* Discard cir frames */
				cir_discard = 1;
			} else {
				cir = DIV_ROUND_UP(cir, 100);
				cir *= 3; /* 33 1/3 kbps */
				cbs = DIV_ROUND_UP(cbs, 4096);
				cbs = (cbs ? cbs : 1); /* No zero burst size */
				cbs_max = 60; /* Limit burst size */
				cf = conf->cf;
				if (cf)
					pir += conf->cir;
			}
		}
		if (pir == 0 && pbs == 0) {
			/* Discard PIR frames */
			pir_discard = 1;
		} else {
			pir = DIV_ROUND_UP(pir, 100);
			pir *= 3;  /* 33 1/3 kbps */
			pbs = DIV_ROUND_UP(pbs, 4096);
			pbs = (pbs ? pbs : 1); /* No zero burst size */
			pbs_max = 60; /* Limit burst size */
		}
		break;
	case MSCC_QOS_RATE_MODE_FRAME:
		if (pir >= 100) {
			frm_mode = POL_MODE_FRMRATE_HI;
			pir = DIV_ROUND_UP(pir, 100);
			pir *= 3;  /* 33 1/3 fps */
			pbs = (pbs * 10) / 328; /* 32.8 frames */
			pbs = (pbs ? pbs : 1); /* No zero burst size */
			pbs_max = GENMASK(6, 0); /* Limit burst size */
		} else {
			frm_mode = POL_MODE_FRMRATE_LO;
			if (pir == 0 && pbs == 0) {
				/* Discard all frames */
				pir_discard = 1;
				cir_discard = 1;
			} else {
				pir *= 3; /* 1/3 fps */
				pbs = (pbs * 10) / 3; /* 0.3 frames */
				pbs = (pbs ? pbs : 1); /* No zero burst size */
				pbs_max = 61; /* Limit burst size */
			}
		}
		break;
	default: /* MSCC_QOS_RATE_MODE_DISABLED */
		/* Disable policer using maximum rate and zero burst */
		pir = GENMASK(15, 0);
		pbs = 0;
		break;
	}

	/* Check limits */
	if (pir > GENMASK(15, 0)) {
		dev_err(ocelot->dev,
			"Invalid pir for policer %u: %u (max %lu)\n",
			pol_ix, pir, GENMASK(15, 0));
		return -EINVAL;
	}

	if (cir > GENMASK(15, 0)) {
		dev_err(ocelot->dev,
			"Invalid cir for policer %u: %u (max %lu)\n",
			pol_ix, cir, GENMASK(15, 0));
		return -EINVAL;
	}

	if (pbs > pbs_max) {
		dev_err(ocelot->dev,
			"Invalid pbs for policer %u: %u (max %u)\n",
			pol_ix, pbs, pbs_max);
		return -EINVAL;
	}

	if (cbs > cbs_max) {
		dev_err(ocelot->dev,
			"Invalid cbs for policer %u: %u (max %u)\n",
			pol_ix, cbs, cbs_max);
		return -EINVAL;
	}

	value = (ANA_POL_MODE_CFG_IPG_SIZE(ipg) |
		 ANA_POL_MODE_CFG_FRM_MODE(frm_mode) |
		 (cf ? ANA_POL_MODE_CFG_DLB_COUPLED : 0) |
		 (cir_ena ? ANA_POL_MODE_CFG_CIR_ENA : 0) |
		 ANA_POL_MODE_CFG_OVERSHOOT_ENA);

	ocelot_write_gix(ocelot, value, ANA_POL_MODE_CFG, pol_ix);

	ocelot_write_gix(ocelot,
			 ANA_POL_PIR_CFG_PIR_RATE(pir) |
			 ANA_POL_PIR_CFG_PIR_BURST(pbs),
			 ANA_POL_PIR_CFG, pol_ix);

	ocelot_write_gix(ocelot,
			 (pir_discard ? GENMASK(22, 0) : 0),
			 ANA_POL_PIR_STATE, pol_ix);

	ocelot_write_gix(ocelot,
			 ANA_POL_CIR_CFG_CIR_RATE(cir) |
			 ANA_POL_CIR_CFG_CIR_BURST(cbs),
			 ANA_POL_CIR_CFG, pol_ix);

	ocelot_write_gix(ocelot,
			 (cir_discard ? GENMASK(22, 0) : 0),
			 ANA_POL_CIR_STATE, pol_ix);

	return 0;
}

int ocelot_policer_validate(const struct flow_action *action,
			    const struct flow_action_entry *a,
			    struct netlink_ext_ack *extack)
{
	if (a->police.exceed.act_id != FLOW_ACTION_DROP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when exceed action is not drop");
		return -EOPNOTSUPP;
	}

	if (a->police.notexceed.act_id != FLOW_ACTION_PIPE &&
	    a->police.notexceed.act_id != FLOW_ACTION_ACCEPT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is not pipe or ok");
		return -EOPNOTSUPP;
	}

	if (a->police.notexceed.act_id == FLOW_ACTION_ACCEPT &&
	    !flow_action_is_last_entry(action, a)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is ok, but police action is not last");
		return -EOPNOTSUPP;
	}

	if (a->police.peakrate_bytes_ps ||
	    a->police.avrate || a->police.overhead) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when peakrate/avrate/overhead is configured");
		return -EOPNOTSUPP;
	}

	if (a->police.rate_pkt_ps) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload does not support packets per second");
		return -EOPNOTSUPP;
	}

	return 0;
}
EXPORT_SYMBOL(ocelot_policer_validate);

int ocelot_port_policer_add(struct ocelot *ocelot, int port,
			    struct ocelot_policer *pol)
{
	struct qos_policer_conf pp = { 0 };
	int err;

	if (!pol)
		return -EINVAL;

	pp.mode = MSCC_QOS_RATE_MODE_DATA;
	pp.pir = pol->rate;
	pp.pbs = pol->burst;

	dev_dbg(ocelot->dev, "%s: port %u pir %u kbps, pbs %u bytes\n",
		__func__, port, pp.pir, pp.pbs);

	err = qos_policer_conf_set(ocelot, POL_IX_PORT + port, &pp);
	if (err)
		return err;

	ocelot_rmw_gix(ocelot,
		       ANA_PORT_POL_CFG_PORT_POL_ENA |
		       ANA_PORT_POL_CFG_POL_ORDER(POL_ORDER),
		       ANA_PORT_POL_CFG_PORT_POL_ENA |
		       ANA_PORT_POL_CFG_POL_ORDER_M,
		       ANA_PORT_POL_CFG, port);

	return 0;
}
EXPORT_SYMBOL(ocelot_port_policer_add);

int ocelot_port_policer_del(struct ocelot *ocelot, int port)
{
	struct qos_policer_conf pp = { 0 };
	int err;

	dev_dbg(ocelot->dev, "%s: port %u\n", __func__, port);

	pp.mode = MSCC_QOS_RATE_MODE_DISABLED;

	err = qos_policer_conf_set(ocelot, POL_IX_PORT + port, &pp);
	if (err)
		return err;

	ocelot_rmw_gix(ocelot,
		       ANA_PORT_POL_CFG_POL_ORDER(POL_ORDER),
		       ANA_PORT_POL_CFG_PORT_POL_ENA |
		       ANA_PORT_POL_CFG_POL_ORDER_M,
		       ANA_PORT_POL_CFG, port);

	return 0;
}
EXPORT_SYMBOL(ocelot_port_policer_del);
