/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/* Copyright(c) 2015-2022 Intel Corporation. */

#ifndef __SDW_INTEL_AUXDEVICE_H
#define __SDW_INTEL_AUXDEVICE_H

int intel_link_startup(struct auxiliary_device *auxdev);
int intel_link_process_wakeen_event(struct auxiliary_device *auxdev);

struct sdw_intel_link_dev {
	struct auxiliary_device auxdev;
	struct sdw_intel_link_res link_res;
};

#define auxiliary_dev_to_sdw_intel_link_dev(auxiliary_dev) \
	container_of(auxiliary_dev, struct sdw_intel_link_dev, auxdev)

#endif /* __SDW_INTEL_AUXDEVICE_H */
