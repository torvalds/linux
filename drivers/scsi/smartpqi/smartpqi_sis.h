/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    driver for Microsemi PQI-based storage controllers
 *    Copyright (c) 2019-2020 Microchip Technology Inc. and its subsidiaries
 *    Copyright (c) 2016-2018 Microsemi Corporation
 *    Copyright (c) 2016 PMC-Sierra, Inc.
 *
 *    Questions/Comments/Bugfixes to storagedev@microchip.com
 *
 */

#if !defined(_SMARTPQI_SIS_H)
#define _SMARTPQI_SIS_H

int sis_wait_for_ctrl_ready(struct pqi_ctrl_info *ctrl_info);
int sis_wait_for_ctrl_ready_resume(struct pqi_ctrl_info *ctrl_info);
bool sis_is_firmware_running(struct pqi_ctrl_info *ctrl_info);
bool sis_is_kernel_up(struct pqi_ctrl_info *ctrl_info);
int sis_get_ctrl_properties(struct pqi_ctrl_info *ctrl_info);
int sis_get_pqi_capabilities(struct pqi_ctrl_info *ctrl_info);
int sis_init_base_struct_addr(struct pqi_ctrl_info *ctrl_info);
void sis_enable_msix(struct pqi_ctrl_info *ctrl_info);
void sis_enable_intx(struct pqi_ctrl_info *ctrl_info);
void sis_shutdown_ctrl(struct pqi_ctrl_info *ctrl_info);
int sis_pqi_reset_quiesce(struct pqi_ctrl_info *ctrl_info);
int sis_reenable_sis_mode(struct pqi_ctrl_info *ctrl_info);
void sis_write_driver_scratch(struct pqi_ctrl_info *ctrl_info, u32 value);
u32 sis_read_driver_scratch(struct pqi_ctrl_info *ctrl_info);
void sis_soft_reset(struct pqi_ctrl_info *ctrl_info);

#endif	/* _SMARTPQI_SIS_H */
