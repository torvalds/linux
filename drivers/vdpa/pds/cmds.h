/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#ifndef _VDPA_CMDS_H_
#define _VDPA_CMDS_H_

int pds_vdpa_init_hw(struct pds_vdpa_device *pdsv);

int pds_vdpa_cmd_reset(struct pds_vdpa_device *pdsv);
int pds_vdpa_cmd_set_status(struct pds_vdpa_device *pdsv, u8 status);
int pds_vdpa_cmd_set_mac(struct pds_vdpa_device *pdsv, u8 *mac);
int pds_vdpa_cmd_set_max_vq_pairs(struct pds_vdpa_device *pdsv, u16 max_vqp);
int pds_vdpa_cmd_init_vq(struct pds_vdpa_device *pdsv, u16 qid, u16 invert_idx,
			 struct pds_vdpa_vq_info *vq_info);
int pds_vdpa_cmd_reset_vq(struct pds_vdpa_device *pdsv, u16 qid, u16 invert_idx,
			  struct pds_vdpa_vq_info *vq_info);
#endif /* _VDPA_CMDS_H_ */
