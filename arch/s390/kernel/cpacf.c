// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2024
 */

#define KMSG_COMPONENT "cpacf"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <asm/cpacf.h>

#define CPACF_QUERY(name, instruction)						\
static ssize_t name##_query_raw_read(struct file *fp,				\
				     struct kobject *kobj,			\
				     const struct bin_attribute *attr,		\
				     char *buf, loff_t offs,			\
				     size_t count)				\
{										\
	cpacf_mask_t mask;							\
										\
	if (!cpacf_query(CPACF_##instruction, &mask))				\
		return -EOPNOTSUPP;						\
	return memory_read_from_buffer(buf, count, &offs, &mask, sizeof(mask));	\
}										\
static const BIN_ATTR_RO(name##_query_raw, sizeof(cpacf_mask_t))

CPACF_QUERY(km, KM);
CPACF_QUERY(kmc, KMC);
CPACF_QUERY(kimd, KIMD);
CPACF_QUERY(klmd, KLMD);
CPACF_QUERY(kmac, KMAC);
CPACF_QUERY(pckmo, PCKMO);
CPACF_QUERY(kmf, KMF);
CPACF_QUERY(kmctr, KMCTR);
CPACF_QUERY(kmo, KMO);
CPACF_QUERY(pcc, PCC);
CPACF_QUERY(prno, PRNO);
CPACF_QUERY(kma, KMA);
CPACF_QUERY(kdsa, KDSA);

#define CPACF_QAI(name, instruction)					\
static ssize_t name##_query_auth_info_raw_read(				\
	struct file *fp, struct kobject *kobj,				\
	const struct bin_attribute *attr, char *buf, loff_t offs,	\
	size_t count)							\
{									\
	cpacf_qai_t qai;						\
									\
	if (!cpacf_qai(CPACF_##instruction, &qai))			\
		return -EOPNOTSUPP;					\
	return memory_read_from_buffer(buf, count, &offs, &qai,		\
					sizeof(qai));			\
}									\
static const BIN_ATTR_RO(name##_query_auth_info_raw, sizeof(cpacf_qai_t))

CPACF_QAI(km, KM);
CPACF_QAI(kmc, KMC);
CPACF_QAI(kimd, KIMD);
CPACF_QAI(klmd, KLMD);
CPACF_QAI(kmac, KMAC);
CPACF_QAI(pckmo, PCKMO);
CPACF_QAI(kmf, KMF);
CPACF_QAI(kmctr, KMCTR);
CPACF_QAI(kmo, KMO);
CPACF_QAI(pcc, PCC);
CPACF_QAI(prno, PRNO);
CPACF_QAI(kma, KMA);
CPACF_QAI(kdsa, KDSA);

static const struct bin_attribute *const cpacf_attrs[] = {
	&bin_attr_km_query_raw,
	&bin_attr_kmc_query_raw,
	&bin_attr_kimd_query_raw,
	&bin_attr_klmd_query_raw,
	&bin_attr_kmac_query_raw,
	&bin_attr_pckmo_query_raw,
	&bin_attr_kmf_query_raw,
	&bin_attr_kmctr_query_raw,
	&bin_attr_kmo_query_raw,
	&bin_attr_pcc_query_raw,
	&bin_attr_prno_query_raw,
	&bin_attr_kma_query_raw,
	&bin_attr_kdsa_query_raw,
	&bin_attr_km_query_auth_info_raw,
	&bin_attr_kmc_query_auth_info_raw,
	&bin_attr_kimd_query_auth_info_raw,
	&bin_attr_klmd_query_auth_info_raw,
	&bin_attr_kmac_query_auth_info_raw,
	&bin_attr_pckmo_query_auth_info_raw,
	&bin_attr_kmf_query_auth_info_raw,
	&bin_attr_kmctr_query_auth_info_raw,
	&bin_attr_kmo_query_auth_info_raw,
	&bin_attr_pcc_query_auth_info_raw,
	&bin_attr_prno_query_auth_info_raw,
	&bin_attr_kma_query_auth_info_raw,
	&bin_attr_kdsa_query_auth_info_raw,
	NULL,
};

static const struct attribute_group cpacf_attr_grp = {
	.name = "cpacf",
	.bin_attrs = cpacf_attrs,
};

static int __init cpacf_init(void)
{
	struct device *cpu_root;
	int rc = 0;

	cpu_root = bus_get_dev_root(&cpu_subsys);
	if (cpu_root) {
		rc = sysfs_create_group(&cpu_root->kobj, &cpacf_attr_grp);
		put_device(cpu_root);
	}
	return rc;
}
device_initcall(cpacf_init);
