/* arch/arm/mach-msm/qdsp5/adsp_6225.h
 *
 * Copyright (c) 2008 QUALCOMM Incorporated.
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
typedef enum {
	QDSP_MODULE_KERNEL,
	QDSP_MODULE_AFETASK,
	QDSP_MODULE_AUDPLAY0TASK,
	QDSP_MODULE_AUDPLAY1TASK,
	QDSP_MODULE_AUDPPTASK,
	QDSP_MODULE_VIDEOTASK,
	QDSP_MODULE_VIDEO_AAC_VOC,
	QDSP_MODULE_PCM_DEC,
	QDSP_MODULE_AUDIO_DEC_MP3,
	QDSP_MODULE_AUDIO_DEC_AAC,
	QDSP_MODULE_AUDIO_DEC_WMA,
	QDSP_MODULE_HOSTPCM,
	QDSP_MODULE_DTMF,
	QDSP_MODULE_AUDRECTASK,
	QDSP_MODULE_AUDPREPROCTASK,
	QDSP_MODULE_SBC_ENC,
	QDSP_MODULE_VOC_UMTS,
	QDSP_MODULE_VOC_CDMA,
	QDSP_MODULE_VOC_PCM,
	QDSP_MODULE_VOCENCTASK,
	QDSP_MODULE_VOCDECTASK,
	QDSP_MODULE_VOICEPROCTASK,
	QDSP_MODULE_VIDEOENCTASK,
	QDSP_MODULE_VFETASK,
	QDSP_MODULE_WAV_ENC,
	QDSP_MODULE_AACLC_ENC,
	QDSP_MODULE_VIDEO_AMR,
	QDSP_MODULE_VOC_AMR,
	QDSP_MODULE_VOC_EVRC,
	QDSP_MODULE_VOC_13K,
	QDSP_MODULE_VOC_FGV,
	QDSP_MODULE_DIAGTASK,
	QDSP_MODULE_JPEGTASK,
	QDSP_MODULE_LPMTASK,
	QDSP_MODULE_QCAMTASK,
	QDSP_MODULE_MODMATHTASK,
	QDSP_MODULE_AUDPLAY2TASK,
	QDSP_MODULE_AUDPLAY3TASK,
	QDSP_MODULE_AUDPLAY4TASK,
	QDSP_MODULE_GRAPHICSTASK,
	QDSP_MODULE_MIDI,
	QDSP_MODULE_GAUDIO,
	QDSP_MODULE_VDEC_LP_MODE,
	QDSP_MODULE_MAX,
} qdsp_module_type;

#define QDSP_RTOS_MAX_TASK_ID  30U

/* Table of modules indexed by task ID for the GAUDIO image */
static qdsp_module_type qdsp_gaudio_task_to_module_table[] = {
	QDSP_MODULE_KERNEL,
	QDSP_MODULE_AFETASK,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_AUDPPTASK,
	QDSP_MODULE_AUDPLAY0TASK,
	QDSP_MODULE_AUDPLAY1TASK,
	QDSP_MODULE_AUDPLAY2TASK,
	QDSP_MODULE_AUDPLAY3TASK,
	QDSP_MODULE_AUDPLAY4TASK,
	QDSP_MODULE_MAX,
	QDSP_MODULE_AUDRECTASK,
	QDSP_MODULE_AUDPREPROCTASK,
	QDSP_MODULE_MAX,
	QDSP_MODULE_GRAPHICSTASK,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
};

/* Queue offset table indexed by queue ID for the GAUDIO image */
static uint32_t qdsp_gaudio_queue_offset_table[] = {
	QDSP_RTOS_NO_QUEUE,  /* QDSP_lpmCommandQueue              */
	0x3f0,               /* QDSP_mpuAfeQueue                  */
	0x420,               /* QDSP_mpuGraphicsCmdQueue          */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuModmathCmdQueue           */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuVDecCmdQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuVDecPktQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuVEncCmdQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_rxMpuDecCmdQueue             */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_rxMpuDecPktQueue             */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_txMpuEncQueue                */
	0x3f4,               /* QDSP_uPAudPPCmd1Queue             */
	0x3f8,               /* QDSP_uPAudPPCmd2Queue             */
	0x3fc,               /* QDSP_uPAudPPCmd3Queue             */
	0x40c,               /* QDSP_uPAudPlay0BitStreamCtrlQueue */
	0x410,               /* QDSP_uPAudPlay1BitStreamCtrlQueue */
	0x414,               /* QDSP_uPAudPlay2BitStreamCtrlQueue */
	0x418,               /* QDSP_uPAudPlay3BitStreamCtrlQueue */
	0x41c,               /* QDSP_uPAudPlay4BitStreamCtrlQueue */
	0x400,               /* QDSP_uPAudPreProcCmdQueue         */
	0x408,               /* QDSP_uPAudRecBitStreamQueue       */
	0x404,               /* QDSP_uPAudRecCmdQueue             */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPJpegActionCmdQueue         */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPJpegCfgCmdQueue            */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPVocProcQueue               */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_vfeCommandQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_vfeCommandScaleQueue         */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_vfeCommandTableQueue         */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPDiagQueue                  */
};

/* Table of modules indexed by task ID for the COMBO image */
static qdsp_module_type qdsp_combo_task_to_module_table[] = {
	QDSP_MODULE_KERNEL,
	QDSP_MODULE_AFETASK,
	QDSP_MODULE_VOCDECTASK,
	QDSP_MODULE_VOCENCTASK,
	QDSP_MODULE_VIDEOTASK,
	QDSP_MODULE_VIDEOENCTASK,
	QDSP_MODULE_VOICEPROCTASK,
	QDSP_MODULE_VFETASK,
	QDSP_MODULE_JPEGTASK,
	QDSP_MODULE_AUDPPTASK,
	QDSP_MODULE_AUDPLAY0TASK,
	QDSP_MODULE_AUDPLAY1TASK,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_LPMTASK,
	QDSP_MODULE_AUDRECTASK,
	QDSP_MODULE_AUDPREPROCTASK,
	QDSP_MODULE_MODMATHTASK,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_DIAGTASK,
	QDSP_MODULE_MAX,
};

/* Queue offset table indexed by queue ID for the COMBO image */
static uint32_t qdsp_combo_queue_offset_table[] = {
	0x714,               /* QDSP_lpmCommandQueue              */
	0x6bc,               /* QDSP_mpuAfeQueue                  */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuGraphicsCmdQueue          */
	0x6d0,               /* QDSP_mpuModmathCmdQueue           */
	0x6e8,               /* QDSP_mpuVDecCmdQueue              */
	0x6ec,               /* QDSP_mpuVDecPktQueue              */
	0x6e4,               /* QDSP_mpuVEncCmdQueue              */
	0x6c4,               /* QDSP_rxMpuDecCmdQueue             */
	0x6c8,               /* QDSP_rxMpuDecPktQueue             */
	0x6cc,               /* QDSP_txMpuEncQueue                */
	0x6f0,               /* QDSP_uPAudPPCmd1Queue             */
	0x6f4,               /* QDSP_uPAudPPCmd2Queue             */
	0x6f8,               /* QDSP_uPAudPPCmd3Queue             */
	0x708,               /* QDSP_uPAudPlay0BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay1BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay2BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay3BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay4BitStreamCtrlQueue */
	0x6fc,               /* QDSP_uPAudPreProcCmdQueue         */
	0x704,               /* QDSP_uPAudRecBitStreamQueue       */
	0x700,               /* QDSP_uPAudRecCmdQueue             */
	0x710,               /* QDSP_uPJpegActionCmdQueue         */
	0x70c,               /* QDSP_uPJpegCfgCmdQueue            */
	0x6c0,               /* QDSP_uPVocProcQueue               */
	0x6d8,               /* QDSP_vfeCommandQueue              */
	0x6e0,               /* QDSP_vfeCommandScaleQueue         */
	0x6dc,               /* QDSP_vfeCommandTableQueue         */
	0x6d4,               /* QDSP_uPDiagQueue                  */
};

/* Table of modules indexed by task ID for the QTV_LP image */
static qdsp_module_type qdsp_qtv_lp_task_to_module_table[] = {
	QDSP_MODULE_KERNEL,
	QDSP_MODULE_AFETASK,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_VIDEOTASK,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_AUDPPTASK,
	QDSP_MODULE_AUDPLAY0TASK,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_AUDRECTASK,
	QDSP_MODULE_AUDPREPROCTASK,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
	QDSP_MODULE_MAX,
};

/* Queue offset table indexed by queue ID for the QTV_LP image */
static uint32_t qdsp_qtv_lp_queue_offset_table[] = {
	QDSP_RTOS_NO_QUEUE,  /* QDSP_lpmCommandQueue              */
	0x3fe,               /* QDSP_mpuAfeQueue                  */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuGraphicsCmdQueue          */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuModmathCmdQueue           */
	0x402,               /* QDSP_mpuVDecCmdQueue              */
	0x406,               /* QDSP_mpuVDecPktQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuVEncCmdQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_rxMpuDecCmdQueue             */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_rxMpuDecPktQueue             */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_txMpuEncQueue                */
	0x40e,               /* QDSP_uPAudPPCmd1Queue             */
	0x412,               /* QDSP_uPAudPPCmd2Queue             */
	0x416,               /* QDSP_uPAudPPCmd3Queue             */
	0x422,               /* QDSP_uPAudPlay0BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay1BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay2BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay3BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay4BitStreamCtrlQueue */
	0x40a,               /* QDSP_uPAudPreProcCmdQueue         */
	0x41e,               /* QDSP_uPAudRecBitStreamQueue       */
	0x41a,               /* QDSP_uPAudRecCmdQueue             */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPJpegActionCmdQueue         */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPJpegCfgCmdQueue            */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPVocProcQueue               */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_vfeCommandQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_vfeCommandScaleQueue         */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_vfeCommandTableQueue         */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPDiagQueue                  */
};

/* Tables to convert tasks to modules */
static qdsp_module_type *qdsp_task_to_module[] = {
	qdsp_combo_task_to_module_table,
	qdsp_gaudio_task_to_module_table,
	qdsp_qtv_lp_task_to_module_table,
};

/* Tables to retrieve queue offsets */
static uint32_t *qdsp_queue_offset_table[] = {
	qdsp_combo_queue_offset_table,
	qdsp_gaudio_queue_offset_table,
	qdsp_qtv_lp_queue_offset_table,
};

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
	QDSP_MODULE(JPEGTASK, "vdc_clk", 0, adsp_jpeg_verify_cmd,
		adsp_jpeg_patch_event),
	QDSP_MODULE(VIDEOTASK, "vdc_clk", 96000000,
		adsp_video_verify_cmd, NULL),
	QDSP_MODULE(VDEC_LP_MODE, NULL, 0, NULL, NULL),
	QDSP_MODULE(VIDEOENCTASK, "vdc_clk", 96000000,
		adsp_videoenc_verify_cmd, NULL),
};

int adsp_init_info(struct adsp_info *info)
{
	info->send_irq =   0x00c00200;
	info->read_ctrl =  0x00400038;
	info->write_ctrl = 0x00400034;

	info->max_msg16_size = 193;
	info->max_msg32_size = 8;

	info->max_task_id = 16;
	info->max_module_id = QDSP_MODULE_MAX - 1;
	info->max_queue_id = QDSP_QUEUE_MAX;
	info->max_image_id = 2;
	info->queue_offset = qdsp_queue_offset_table;
	info->task_to_module = qdsp_task_to_module;

	info->module_count = ARRAY_SIZE(module_info);
	info->module = module_info;
	return 0;
}
