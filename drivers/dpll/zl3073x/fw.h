/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_FW_H
#define _ZL3073X_FW_H

/*
 * enum zl3073x_fw_component_id - Identifiers for possible flash components
 */
enum zl3073x_fw_component_id {
	ZL_FW_COMPONENT_INVALID = -1,
	ZL_FW_COMPONENT_UTIL = 0,
	ZL_FW_COMPONENT_FW1,
	ZL_FW_COMPONENT_FW2,
	ZL_FW_COMPONENT_FW3,
	ZL_FW_COMPONENT_CFG0,
	ZL_FW_COMPONENT_CFG1,
	ZL_FW_COMPONENT_CFG2,
	ZL_FW_COMPONENT_CFG3,
	ZL_FW_COMPONENT_CFG4,
	ZL_FW_COMPONENT_CFG5,
	ZL_FW_COMPONENT_CFG6,
	ZL_FW_NUM_COMPONENTS
};

/**
 * struct zl3073x_fw_component - Firmware component
 * @id: Flash component ID
 * @size: Size of the buffer
 * @data: Pointer to buffer with component data
 */
struct zl3073x_fw_component {
	enum zl3073x_fw_component_id	id;
	size_t				size;
	void				*data;
};

/**
 * struct zl3073x_fw - Firmware bundle
 * @component: firmware components array
 */
struct zl3073x_fw {
	struct zl3073x_fw_component	*component[ZL_FW_NUM_COMPONENTS];
};

struct zl3073x_fw *zl3073x_fw_load(struct zl3073x_dev *zldev, const char *data,
				   size_t size, struct netlink_ext_ack *extack);
void zl3073x_fw_free(struct zl3073x_fw *fw);

int zl3073x_fw_flash(struct zl3073x_dev *zldev, struct zl3073x_fw *zlfw,
		     struct netlink_ext_ack *extack);

#endif /* _ZL3073X_FW_H */
