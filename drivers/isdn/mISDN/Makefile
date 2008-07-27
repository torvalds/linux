#
# Makefile for the modular ISDN driver
#

obj-$(CONFIG_MISDN) += mISDN_core.o
obj-$(CONFIG_MISDN_DSP) += mISDN_dsp.o
obj-$(CONFIG_MISDN_L1OIP) += l1oip.o

# multi objects

mISDN_core-objs := core.o fsm.o socket.o hwchannel.o stack.o layer1.o layer2.o tei.o timerdev.o
mISDN_dsp-objs := dsp_core.o dsp_cmx.o dsp_tones.o dsp_dtmf.o dsp_audio.o dsp_blowfish.o dsp_pipeline.o dsp_hwec.o
l1oip-objs := l1oip_core.o l1oip_codec.o
