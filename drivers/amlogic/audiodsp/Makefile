
# Makefile for osd and tv 

audiodsp-objs=	audiodsp_module.o \
				dsp_control.o 		\
				dsp_microcode.o 	\
				dsp_mailbox.o			\
				dsp_monitor.o			\
				dsp_codec.o
#audiodsp-objs += pcmenc_module.o pcmenc_stream.o
#audiodsp-objs += spdif_module.o

ifneq ($(KBUILD_SRC),)
TOP_KBUILD_SRC := $(KBUILD_SRC)/
endif

$(obj)/dsp_data.o:
	@cp $(TOP_KBUILD_SRC)drivers/amlogic/audiodsp/dsp_data.z \
        drivers/amlogic/audiodsp/dsp_data.o
audiodsp-objs += dsp_data.o

obj-$(CONFIG_AML_AUDIO_DSP)    +=audiodsp.o

#EXTRA_CFLAGS = -DENABLE_WAIT_FORMAT
