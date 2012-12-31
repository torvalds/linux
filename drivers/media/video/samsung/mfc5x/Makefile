obj-$(CONFIG_VIDEO_MFC5X) += mfc_dev.o
obj-$(CONFIG_VIDEO_MFC5X) += mfc_dec.o
obj-$(CONFIG_VIDEO_MFC5X) += mfc_enc.o
obj-$(CONFIG_VIDEO_MFC5X) += mfc_inst.o
obj-$(CONFIG_VIDEO_MFC5X) += mfc_cmd.o
obj-$(CONFIG_VIDEO_MFC5X) += mfc_shm.o
obj-$(CONFIG_VIDEO_MFC5X) += mfc_reg.o
obj-$(CONFIG_VIDEO_MFC5X) += mfc_buf.o
obj-$(CONFIG_VIDEO_MFC5X) += mfc_pm.o
obj-$(CONFIG_VIDEO_MFC5X) += mfc_ctrl.o
obj-$(CONFIG_VIDEO_MFC5X) += mfc_mem.o

ifeq ($(CONFIG_VIDEO_MFC5X_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

ifeq ($(CONFIG_VIDEO_MFC_VCM_UMP),y)
EXTRA_CFLAGS += -Idrivers/media/video/samsung/ump/include
endif
