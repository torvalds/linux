ccflags-y:=-Idrivers/staging/dream/include
obj-y += adsp.o
ifeq ($(CONFIG_MSM_AMSS_VERSION_6350),y)
obj-y += adsp_info.o
obj-y += audio_evrc.o audio_qcelp.o audio_amrnb.o audio_aac.o
else
obj-y += adsp_6225.o
endif

obj-y += adsp_driver.o
obj-y += adsp_video_verify_cmd.o
obj-y += adsp_videoenc_verify_cmd.o
obj-y += adsp_jpeg_verify_cmd.o adsp_jpeg_patch_event.o
obj-y += adsp_vfe_verify_cmd.o adsp_vfe_patch_event.o
obj-y += adsp_lpm_verify_cmd.o
obj-y += audio_out.o audio_in.o audio_mp3.o audmgr.o audpp.o
obj-y += snd.o

