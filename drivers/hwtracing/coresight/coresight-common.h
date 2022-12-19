/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _CORESIGHT_COMMON_H
#define _CORESIGHT_COMMON_H

#define BM(lsb, msb)		((BIT(msb) - BIT(lsb)) + BIT(msb))
#define BVAL(val, n)            ((val & BIT(n)) >> n)

struct coresight_csr {
	const char *name;
	struct list_head link;
};

#if IS_ENABLED(CONFIG_CORESIGHT_CSR)
extern void msm_qdss_csr_enable_bam_to_usb(struct coresight_csr *csr);
extern void msm_qdss_csr_enable_flush(struct coresight_csr *csr);
extern void msm_qdss_csr_disable_bam_to_usb(struct coresight_csr *csr);
extern void msm_qdss_csr_disable_flush(struct coresight_csr *csr);
extern void msm_qdss_csr_enable_eth(struct coresight_csr *csr);
extern void msm_qdss_csr_disable_eth(struct coresight_csr *csr);
extern int coresight_csr_hwctrl_set(struct coresight_csr *csr, uint64_t addr,
				 uint32_t val);
extern void coresight_csr_set_byte_cntr(struct coresight_csr *csr, int irqctrl_offset,
				 uint32_t count);
extern struct coresight_csr *coresight_csr_get(const char *name);
extern const struct csr_set_atid_op csr_atid_ops;
#if IS_ENABLED(CONFIG_OF)
extern int of_get_coresight_csr_name(struct device_node *node,
				const char **csr_name);
#else
static inline int of_get_coresight_csr_name(struct device_node *node,
		const char **csr_name){ return -EINVAL; }
#endif

#else
static inline void msm_qdss_csr_enable_bam_to_usb(struct coresight_csr *csr) {}
static inline void msm_qdss_csr_disable_bam_to_usb(struct coresight_csr *csr) {}
static inline void msm_qdss_csr_disable_flush(struct coresight_csr *csr) {}
static inline void msm_qdss_csr_enable_eth(struct coresight_csr *csr) {}
static inline void msm_qdss_csr_disable_eth(struct coresight_csr *csr) {}
static inline int coresight_csr_hwctrl_set(struct coresight_csr *csr,
	uint64_t addr, uint32_t val) { return -EINVAL; }
static inline void coresight_csr_set_byte_cntr(struct coresight_csr *csr, int irqctrl_offset,
					   uint32_t count) {}
static inline struct coresight_csr *coresight_csr_get(const char *name)
					{ return NULL; }
static inline int of_get_coresight_csr_name(struct device_node *node,
		const char **csr_name){ return -EINVAL; }
#endif
#if IS_ENABLED(CONFIG_CORESIGHT_CTI) && IS_ENABLED(CONFIG_OF)
extern struct coresight_cti_data *of_get_coresight_cti_data(
				struct device *dev, struct device_node *node);
#else
static inline struct coresight_cti_data *of_get_coresight_cti_data(
		struct device *dev, struct device_node *node) { return NULL; }
#endif
#endif
