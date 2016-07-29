#ifndef __QCOM_MDT_LOADER_H__
#define __QCOM_MDT_LOADER_H__

#define QCOM_MDT_TYPE_MASK	(7 << 24)
#define QCOM_MDT_TYPE_HASH	(2 << 24)
#define QCOM_MDT_RELOCATABLE	BIT(27)

struct resource_table * qcom_mdt_find_rsc_table(struct rproc *rproc, const struct firmware *fw, int *tablesz);
int qcom_mdt_load(struct rproc *rproc, const struct firmware *fw, const char *fw_name);

int qcom_mdt_parse(const struct firmware *fw, phys_addr_t *fw_addr, size_t *fw_size, bool *fw_relocate);

#endif
