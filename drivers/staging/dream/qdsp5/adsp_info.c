/* arch/arm/mach-msm/adsp_info.c
 *
 * Copyright (c) 2008 QUALCOMM Incorporated.
 * Copyright (c) 2008 QUALCOMM USA, INC.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "adsp.h"

/* Firmware modules */
#define QDSP_MODULE_KERNEL                  0x0106dd4e
#define QDSP_MODULE_AFETASK                 0x0106dd6f
#define QDSP_MODULE_AUDPLAY0TASK            0x0106dd70
#define QDSP_MODULE_AUDPLAY1TASK            0x0106dd71
#define QDSP_MODULE_AUDPPTASK               0x0106dd72
#define QDSP_MODULE_VIDEOTASK               0x0106dd73
#define QDSP_MODULE_VIDEO_AAC_VOC           0x0106dd74
#define QDSP_MODULE_PCM_DEC                 0x0106dd75
#define QDSP_MODULE_AUDIO_DEC_MP3           0x0106dd76
#define QDSP_MODULE_AUDIO_DEC_AAC           0x0106dd77
#define QDSP_MODULE_AUDIO_DEC_WMA           0x0106dd78
#define QDSP_MODULE_HOSTPCM                 0x0106dd79
#define QDSP_MODULE_DTMF                    0x0106dd7a
#define QDSP_MODULE_AUDRECTASK              0x0106dd7b
#define QDSP_MODULE_AUDPREPROCTASK          0x0106dd7c
#define QDSP_MODULE_SBC_ENC                 0x0106dd7d
#define QDSP_MODULE_VOC_UMTS                0x0106dd9a
#define QDSP_MODULE_VOC_CDMA                0x0106dd98
#define QDSP_MODULE_VOC_PCM                 0x0106dd7f
#define QDSP_MODULE_VOCENCTASK              0x0106dd80
#define QDSP_MODULE_VOCDECTASK              0x0106dd81
#define QDSP_MODULE_VOICEPROCTASK           0x0106dd82
#define QDSP_MODULE_VIDEOENCTASK            0x0106dd83
#define QDSP_MODULE_VFETASK                 0x0106dd84
#define QDSP_MODULE_WAV_ENC                 0x0106dd85
#define QDSP_MODULE_AACLC_ENC               0x0106dd86
#define QDSP_MODULE_VIDEO_AMR               0x0106dd87
#define QDSP_MODULE_VOC_AMR                 0x0106dd88
#define QDSP_MODULE_VOC_EVRC                0x0106dd89
#define QDSP_MODULE_VOC_13K                 0x0106dd8a
#define QDSP_MODULE_VOC_FGV                 0x0106dd8b
#define QDSP_MODULE_DIAGTASK                0x0106dd8c
#define QDSP_MODULE_JPEGTASK                0x0106dd8d
#define QDSP_MODULE_LPMTASK                 0x0106dd8e
#define QDSP_MODULE_QCAMTASK                0x0106dd8f
#define QDSP_MODULE_MODMATHTASK             0x0106dd90
#define QDSP_MODULE_AUDPLAY2TASK            0x0106dd91
#define QDSP_MODULE_AUDPLAY3TASK            0x0106dd92
#define QDSP_MODULE_AUDPLAY4TASK            0x0106dd93
#define QDSP_MODULE_GRAPHICSTASK            0x0106dd94
#define QDSP_MODULE_MIDI                    0x0106dd95
#define QDSP_MODULE_GAUDIO                  0x0106dd96
#define QDSP_MODULE_VDEC_LP_MODE            0x0106dd97
#define QDSP_MODULE_MAX                     0x7fffffff

   /* DO NOT USE: Force this enum to be a 32bit type to improve speed */
#define QDSP_MODULE_32BIT_DUMMY 0x10000

static uint32_t *qdsp_task_to_module[IMG_MAX];
static uint32_t	*qdsp_queue_offset_table[IMG_MAX];

#define QDSP_MODULE(n, clkname, clkrate, verify_cmd_func, patch_event_func) \
	{ .name = #n, .pdev_name = "adsp_" #n, .id = QDSP_MODULE_##n, \
	  .clk_name = clkname, .clk_rate = clkrate, \
	  .verify_cmd = verify_cmd_func, .patch_event = patch_event_func }

static struct adsp_module_info module_info[] = {
	QDSP_MODULE(AUDPLAY0TASK, NULL, 0, NULL, NULL),
	QDSP_MODULE(AUDPPTASK, NULL, 0, NULL, NULL),
	QDSP_MODULE(AUDRECTASK, NULL, 0, NULL, NULL),
	QDSP_MODULE(AUDPREPROCTASK, NULL, 0, NULL, NULL),
	QDSP_MODULE(VFETASK, "vfe_clk", 0, adsp_vfe_verify_cmd,
		adsp_vfe_patch_event),
	QDSP_MODULE(QCAMTASK, NULL, 0, NULL, NULL),
	QDSP_MODULE(LPMTASK, NULL, 0, adsp_lpm_verify_cmd, NULL),
	QDSP_MODULE(JPEGTASK, "vdc_clk", 96000000, adsp_jpeg_verify_cmd,
		adsp_jpeg_patch_event),
	QDSP_MODULE(VIDEOTASK, "vdc_clk", 96000000,
		adsp_video_verify_cmd, NULL),
	QDSP_MODULE(VDEC_LP_MODE, NULL, 0, NULL, NULL),
	QDSP_MODULE(VIDEOENCTASK, "vdc_clk", 96000000,
		adsp_videoenc_verify_cmd, NULL),
};

int adsp_init_info(struct adsp_info *info)
{
	uint32_t img_num;

	info->send_irq =   0x00c00200;
	info->read_ctrl =  0x00400038;
	info->write_ctrl = 0x00400034;

	info->max_msg16_size = 193;
	info->max_msg32_size = 8;
	for (img_num = 0; img_num < IMG_MAX; img_num++)
		qdsp_queue_offset_table[img_num] =
		&info->init_info_ptr->queue_offsets[img_num][0];

	for (img_num = 0; img_num < IMG_MAX; img_num++)
		qdsp_task_to_module[img_num] =
		&info->init_info_ptr->task_to_module_tbl[img_num][0];
	info->max_task_id = 30;
	info->max_module_id = QDSP_MODULE_MAX - 1;
	info->max_queue_id = QDSP_MAX_NUM_QUEUES;
	info->max_image_id = 2;
	info->queue_offset = qdsp_queue_offset_table;
	info->task_to_module = qdsp_task_to_module;

	info->module_count = ARRAY_SIZE(module_info);
	info->module = module_info;
	return 0;
}
