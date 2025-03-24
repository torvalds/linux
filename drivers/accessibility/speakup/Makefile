# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_SPEAKUP_SYNTH_ACNTSA) += speakup_acntsa.o
obj-$(CONFIG_SPEAKUP_SYNTH_ACNTPC) += speakup_acntpc.o
obj-$(CONFIG_SPEAKUP_SYNTH_APOLLO) += speakup_apollo.o
obj-$(CONFIG_SPEAKUP_SYNTH_AUDPTR) += speakup_audptr.o
obj-$(CONFIG_SPEAKUP_SYNTH_BNS) += speakup_bns.o
obj-$(CONFIG_SPEAKUP_SYNTH_DECTLK) += speakup_dectlk.o
obj-$(CONFIG_SPEAKUP_SYNTH_DECEXT) += speakup_decext.o
obj-$(CONFIG_SPEAKUP_SYNTH_DECPC) += speakup_decpc.o
obj-$(CONFIG_SPEAKUP_SYNTH_DTLK) += speakup_dtlk.o
obj-$(CONFIG_SPEAKUP_SYNTH_KEYPC) += speakup_keypc.o
obj-$(CONFIG_SPEAKUP_SYNTH_LTLK) += speakup_ltlk.o
obj-$(CONFIG_SPEAKUP_SYNTH_SOFT) += speakup_soft.o
obj-$(CONFIG_SPEAKUP_SYNTH_SPKOUT) += speakup_spkout.o
obj-$(CONFIG_SPEAKUP_SYNTH_TXPRT) += speakup_txprt.o
obj-$(CONFIG_SPEAKUP_SYNTH_DUMMY) += speakup_dummy.o

obj-$(CONFIG_SPEAKUP) += speakup.o
speakup-y := \
	buffers.o \
	devsynth.o \
	i18n.o \
	fakekey.o \
	main.o \
	keyhelp.o \
	kobjects.o \
	selection.o \
	spk_ttyio.o \
	synth.o \
	thread.o \
	varhandlers.o
speakup-$(CONFIG_SPEAKUP_SERIALIO) += serialio.o


clean-files := mapdata.h speakupmap.h


# Generate mapdata.h from headers
hostprogs += makemapdata
makemapdata-objs := makemapdata.o

quiet_cmd_mkmap = MKMAP   $@
      cmd_mkmap = TOPDIR=$(srctree) SPKDIR=$(src) $(obj)/makemapdata > $@

$(obj)/mapdata.h: $(obj)/makemapdata
	$(call cmd,mkmap)


# Generate speakupmap.h from mapdata.h
hostprogs += genmap
genmap-objs := genmap.o
$(obj)/genmap.o: $(obj)/mapdata.h

quiet_cmd_genmap = GENMAP  $@
      cmd_genmap = $(obj)/genmap $< > $@

$(obj)/speakupmap.h: $(src)/speakupmap.map $(obj)/genmap
	$(call cmd,genmap)

$(obj)/main.o: $(obj)/speakupmap.h
