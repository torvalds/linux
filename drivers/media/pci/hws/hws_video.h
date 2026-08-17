/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HWS_VIDEO_H
#define HWS_VIDEO_H

struct hws_video;

int hws_video_register(struct hws_pcie_dev *dev);
void hws_video_unregister(struct hws_pcie_dev *dev);
void hws_enable_video_capture(struct hws_pcie_dev *hws,
			      unsigned int chan,
			      bool on);
void hws_prime_next_locked(struct hws_video *vid);

int hws_video_init_channel(struct hws_pcie_dev *pdev, int ch);
void hws_video_cleanup_channel(struct hws_pcie_dev *pdev, int ch);
void check_video_format(struct hws_pcie_dev *pdx);
int hws_check_card_status(struct hws_pcie_dev *hws);
void hws_init_video_sys(struct hws_pcie_dev *hws, bool enable);

void hws_program_dma_for_addr(struct hws_pcie_dev *hws,
			      unsigned int ch,
			      dma_addr_t dma);
void hws_set_dma_doorbell(struct hws_pcie_dev *hws, unsigned int ch,
			  dma_addr_t dma, const char *tag);

int hws_video_quiesce(struct hws_pcie_dev *hws, const char *reason);
void hws_video_pm_resume(struct hws_pcie_dev *hws);

#endif
