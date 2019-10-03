/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NITROX_HAL_H
#define __NITROX_HAL_H

#include "nitrox_dev.h"

void nitrox_config_aqm_rings(struct nitrox_device *ndev);
void nitrox_config_aqm_unit(struct nitrox_device *ndev);
void nitrox_config_emu_unit(struct nitrox_device *ndev);
void nitrox_config_pkt_input_rings(struct nitrox_device *ndev);
void nitrox_config_pkt_solicit_ports(struct nitrox_device *ndev);
void nitrox_config_nps_core_unit(struct nitrox_device *ndev);
void nitrox_config_nps_pkt_unit(struct nitrox_device *ndev);
void nitrox_config_pom_unit(struct nitrox_device *ndev);
void nitrox_config_rand_unit(struct nitrox_device *ndev);
void nitrox_config_efl_unit(struct nitrox_device *ndev);
void nitrox_config_bmi_unit(struct nitrox_device *ndev);
void nitrox_config_bmo_unit(struct nitrox_device *ndev);
void nitrox_config_lbc_unit(struct nitrox_device *ndev);
void invalidate_lbc(struct nitrox_device *ndev);
void enable_aqm_ring(struct nitrox_device *ndev, int qno);
void enable_pkt_input_ring(struct nitrox_device *ndev, int ring);
void enable_pkt_solicit_port(struct nitrox_device *ndev, int port);
void config_nps_core_vfcfg_mode(struct nitrox_device *ndev, enum vf_mode mode);
void nitrox_get_hwinfo(struct nitrox_device *ndev);
void enable_pf2vf_mbox_interrupts(struct nitrox_device *ndev);
void disable_pf2vf_mbox_interrupts(struct nitrox_device *ndev);

#endif /* __NITROX_HAL_H */
