/*
 * QLogic qlcnic NIC Driver
 * Copyright (c)  2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#ifndef __QLCNIC_DCBX_H
#define __QLCNIC_DCBX_H

#define QLCNIC_DCB_STATE	0
#define QLCNIC_DCB_AEN_MODE	1

#ifdef CONFIG_QLCNIC_DCB
int qlcnic_register_dcb(struct qlcnic_adapter *);
#else
static inline int qlcnic_register_dcb(struct qlcnic_adapter *adapter)
{ return 0; }
#endif

struct qlcnic_dcb;

struct qlcnic_dcb_ops {
	int (*query_hw_capability) (struct qlcnic_dcb *, char *);
	int (*get_hw_capability) (struct qlcnic_dcb *);
	int (*query_cee_param) (struct qlcnic_dcb *, char *, u8);
	void (*init_dcbnl_ops) (struct qlcnic_dcb *);
	void (*aen_handler) (struct qlcnic_dcb *, void *);
	int (*get_cee_cfg) (struct qlcnic_dcb *);
	void (*get_info) (struct qlcnic_dcb *);
	int (*attach) (struct qlcnic_dcb *);
	void (*free) (struct qlcnic_dcb *);
};

struct qlcnic_dcb {
	struct qlcnic_dcb_mbx_params	*param;
	struct qlcnic_adapter		*adapter;
	struct delayed_work		aen_work;
	struct workqueue_struct		*wq;
	const struct qlcnic_dcb_ops	*ops;
	struct qlcnic_dcb_cfg		*cfg;
	unsigned long			state;
};

static inline void qlcnic_clear_dcb_ops(struct qlcnic_dcb *dcb)
{
	kfree(dcb);
}

static inline int qlcnic_dcb_get_hw_capability(struct qlcnic_dcb *dcb)
{
	if (dcb && dcb->ops->get_hw_capability)
		return dcb->ops->get_hw_capability(dcb);

	return 0;
}

static inline void qlcnic_dcb_free(struct qlcnic_dcb *dcb)
{
	if (dcb && dcb->ops->free)
		dcb->ops->free(dcb);
}

static inline int qlcnic_dcb_attach(struct qlcnic_dcb *dcb)
{
	if (dcb && dcb->ops->attach)
		return dcb->ops->attach(dcb);

	return 0;
}

static inline int
qlcnic_dcb_query_hw_capability(struct qlcnic_dcb *dcb, char *buf)
{
	if (dcb && dcb->ops->query_hw_capability)
		return dcb->ops->query_hw_capability(dcb, buf);

	return 0;
}

static inline void qlcnic_dcb_get_info(struct qlcnic_dcb *dcb)
{
	if (dcb && dcb->ops->get_info)
		dcb->ops->get_info(dcb);
}

static inline int
qlcnic_dcb_query_cee_param(struct qlcnic_dcb *dcb, char *buf, u8 type)
{
	if (dcb && dcb->ops->query_cee_param)
		return dcb->ops->query_cee_param(dcb, buf, type);

	return 0;
}

static inline int qlcnic_dcb_get_cee_cfg(struct qlcnic_dcb *dcb)
{
	if (dcb && dcb->ops->get_cee_cfg)
		return dcb->ops->get_cee_cfg(dcb);

	return 0;
}

static inline void qlcnic_dcb_aen_handler(struct qlcnic_dcb *dcb, void *msg)
{
	if (dcb && dcb->ops->aen_handler)
		dcb->ops->aen_handler(dcb, msg);
}

static inline void qlcnic_dcb_init_dcbnl_ops(struct qlcnic_dcb *dcb)
{
	if (dcb && dcb->ops->init_dcbnl_ops)
		dcb->ops->init_dcbnl_ops(dcb);
}

static inline void qlcnic_dcb_enable(struct qlcnic_dcb *dcb)
{
	if (dcb && qlcnic_dcb_attach(dcb))
		qlcnic_clear_dcb_ops(dcb);
}
#endif
