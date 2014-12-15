#
# Makefile for the Silicon Image 835x MHL TX device driver
#

#TARGET = RGB


DRIVER_NAME := sii5293drv


ccflags-y := -I$(src)/include
ccflags-y += -I$(src)/platform/hal

ccflags-y += -I$(src)/../application
#ccflags-y += -I$(src)/../application/app_cec
#ccflags-y += -I$(src)/../component/
ccflags-y += -I$(src)/../component/cbus
#ccflags-y += -I$(src)/../component/cec_manager
ccflags-y += -I$(src)/../component/edid
ccflags-y += -I$(src)/../component/rx
ccflags-y += -I$(src)/../component/video_tables
#ccflags-y += -I$(src)/../driver
ccflags-y += -I$(src)/../driver/board_drv
ccflags-y += -I$(src)/../driver/cbus_drv
#ccflags-y += -I$(src)/../driver/cpi_drv
ccflags-y += -I$(src)/../driver/cra_drv
ccflags-y += -I$(src)/../driver/rx_drv
ccflags-y += -I$(src)/application

ifneq ($(CONFIG_MHAWB_SUPPORT),)
ccflags-y += -I$(src)/../driver/mhawb_drv
ccflags-y += -DMHAWB_SUPPORT
endif
ccflags-y += -DMAKE_5293_DRIVER

ccflags-$(CONFIG_DEBUG_DRIVER) += -DDEBUG

ccflags-$(CONFIG_CEC_DRIVER) += -DINC_CEC

obj-$(CONFIG_SII5293_RX) += $(DRIVER_NAME).o

#----------------- App layer --------------------------------------
$(DRIVER_NAME)-objs  += application/mhl_linuxdrv_main.o
$(DRIVER_NAME)-objs  += application/sii5293_interface.o
$(DRIVER_NAME)-objs  += application/vdin_interface.o
$(DRIVER_NAME)-objs  += ../application/sk_app_cbus.o
#$(DRIVER_NAME)-objs  += ../application/app_cec/sk_app_cec.o
#----------------- Component layer --------------------------------
$(DRIVER_NAME)-objs  += ../component/cbus/si_cbus_component.o
#$(DRIVER_NAME)-objs  += ../component/cec_manager/si_cec_component.o
#$(DRIVER_NAME)-objs  += ../component/cec_manager/si_cec_controller.o
#$(DRIVER_NAME)-objs  += ../component/cec_manager/si_cec_rx.o
#$(DRIVER_NAME)-objs  += ../component/cec_manager/si_cec_timer.o
#$(DRIVER_NAME)-objs  += ../component/cec_manager/si_cec_tx.o
$(DRIVER_NAME)-objs  += ../component/rx/si_rx_audio.o
$(DRIVER_NAME)-objs  += ../component/rx/si_rx_info.o
$(DRIVER_NAME)-objs  += ../component/rx/si_rx_video_mode_detection.o
$(DRIVER_NAME)-objs  += ../component/video_tables/si_video_tables.o
#----------------- Driver layer --------------------------------
$(DRIVER_NAME)-objs  += ../driver/board_drv/si_drv_board.o
$(DRIVER_NAME)-objs  += ../driver/board_drv/si_drv_evita.o
$(DRIVER_NAME)-objs  += ../driver/cbus_drv/si_drv_cbus.o
#$(DRIVER_NAME)-objs  += ../driver/cpi_drv/si_drv_cpi.o
$(DRIVER_NAME)-objs  += ../driver/cra_drv/si_cra.o
$(DRIVER_NAME)-objs  += ../driver/cra_drv/si_drv_cra_cfg.o
$(DRIVER_NAME)-objs  += ../driver/rx_drv/si_drv_rx.o
$(DRIVER_NAME)-objs  += ../driver/rx_drv/si_drv_rx_info.o
$(DRIVER_NAME)-objs  += ../driver/rx_drv/si_drv_rx_isr.o
$(DRIVER_NAME)-objs  += ../driver/rx_drv/si_drv_device.o
#----------------- Platform layer ---------------------------------
$(DRIVER_NAME)-objs  += platform/osal/sii_osal_linux_timer.o
$(DRIVER_NAME)-objs  += platform/osal/si_osdebug.o
$(DRIVER_NAME)-objs  += platform/hal/sii_hal_linux_init.o
$(DRIVER_NAME)-objs  += platform/hal/sii_hal_linux_i2c.o
$(DRIVER_NAME)-objs  += platform/hal/sii_hal_linux_isr.o
$(DRIVER_NAME)-objs  += platform/hal/sii_hal_linux_timer.o
$(DRIVER_NAME)-objs  += platform/hal/sii_hal_linux_gpio.o

ifneq ($(CONFIG_MHAWB_SUPPORT),)
$(DRIVER_NAME)-objs  += ../driver/mhawb_drv/si_drv_hawb.o
$(DRIVER_NAME)-objs  += ../driver/mhawb_drv/si_drv_mhawb.o
endif

