#
# kbuild file for firmware/
#

# Create $(fwabs) from $(CONFIG_EXTRA_FIRMWARE_DIR) -- if it doesn't have a
# leading /, it's relative to $(srctree).
fwdir := $(subst ",,$(CONFIG_EXTRA_FIRMWARE_DIR))
fwabs := $(addprefix $(srctree)/,$(filter-out /%,$(fwdir)))$(filter /%,$(fwdir))

fw-external-y := $(subst ",,$(CONFIG_EXTRA_FIRMWARE))

# There are three cases to care about:
# 1. Building kernel with CONFIG_FIRMWARE_IN_KERNEL=y -- $(fw-shipped-y) should
#    include the firmware files to include, according to .config
# 2. 'make modules_install', which will install firmware for modules, and 
#    _also_ for the in-kernel drivers when CONFIG_FIRMWARE_IN_KERNEL=n
# 3. 'make firmware_install', which installs all firmware, unconditionally.

# For the former two cases we want $(fw-shipped-y) and $(fw-shipped-m) to be
# accurate. In the latter case it doesn't matter -- it'll use $(fw-shipped-all).
# But be aware that the config file might not be included at all.

ifdef CONFIG_ACENIC_OMIT_TIGON_I
acenic-objs := acenic/tg2.bin
fw-shipped- += acenic/tg1.bin
else
acenic-objs := acenic/tg1.bin acenic/tg2.bin
endif
fw-shipped-$(CONFIG_ACENIC) += $(acenic-objs)
fw-shipped-$(CONFIG_ADAPTEC_STARFIRE) += adaptec/starfire_rx.bin \
					 adaptec/starfire_tx.bin
fw-shipped-$(CONFIG_ATARI_DSP56K) += dsp56k/bootstrap.bin
fw-shipped-$(CONFIG_ATM_AMBASSADOR) += atmsar11.fw
fw-shipped-$(CONFIG_CASSINI) += sun/cassini.bin
fw-shipped-$(CONFIG_COMPUTONE) += intelliport2.bin
fw-shipped-$(CONFIG_CHELSIO_T3) += cxgb3/t3b_psram-1.1.0.bin \
				   cxgb3/t3c_psram-1.1.0.bin \
				   cxgb3/t3fw-7.0.0.bin
fw-shipped-$(CONFIG_DVB_TTUSB_BUDGET) += ttusb-budget/dspbootcode.bin
fw-shipped-$(CONFIG_E100) += e100/d101m_ucode.bin e100/d101s_ucode.bin \
			     e100/d102e_ucode.bin
fw-shipped-$(CONFIG_SMCTR) += tr_smctr.bin
fw-shipped-$(CONFIG_SND_KORG1212) += korg/k1212.dsp
fw-shipped-$(CONFIG_SND_MAESTRO3) += ess/maestro3_assp_kernel.fw \
				     ess/maestro3_assp_minisrc.fw
fw-shipped-$(CONFIG_SND_SB16_CSP) += sb16/mulaw_main.csp sb16/alaw_main.csp \
				     sb16/ima_adpcm_init.csp \
				     sb16/ima_adpcm_playback.csp \
				     sb16/ima_adpcm_capture.csp
fw-shipped-$(CONFIG_SND_YMFPCI) += yamaha/ds1_ctrl.fw yamaha/ds1_dsp.fw \
				   yamaha/ds1e_ctrl.fw
fw-shipped-$(CONFIG_TIGON3) += tigon/tg3.bin tigon/tg3_tso.bin \
			       tigon/tg3_tso5.bin
fw-shipped-$(CONFIG_USB_DABUSB) += dabusb/firmware.fw dabusb/bitstream.bin
fw-shipped-$(CONFIG_USB_EMI26) += emi26/loader.fw emi26/firmware.fw \
				  emi26/bitstream.fw
fw-shipped-$(CONFIG_USB_EMI62) += emi62/loader.fw emi62/bitstream.fw \
				  emi62/spdif.fw emi62/midi.fw
fw-shipped-$(CONFIG_USB_KAWETH) += kaweth/new_code.bin kaweth/trigger_code.bin \
				   kaweth/new_code_fix.bin \
				   kaweth/trigger_code_fix.bin
ifdef CONFIG_FIRMWARE_IN_KERNEL
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_MPR) += keyspan/mpr.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_USA18X) += keyspan/usa18x.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_USA19) += keyspan/usa19.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_USA19QI) += keyspan/usa19qi.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_USA19QW) += keyspan/usa19qw.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_USA19W) += keyspan/usa19w.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_USA28) += keyspan/usa28.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_USA28XA) += keyspan/usa28xa.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_USA28XB) += keyspan/usa28xb.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_USA28X) += keyspan/usa28x.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_USA49W) += keyspan/usa49w.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_USA49WLC) += keyspan/usa49wlc.fw
else
fw-shipped- += keyspan/mpr.fw keyspan/usa18x.fw keyspan/usa19.fw	\
	keyspan/usa19qi.fw keyspan/usa19qw.fw keyspan/usa19w.fw		\
	keyspan/usa28.fw keyspan/usa28xa.fw keyspan/usa28xb.fw		\
	keyspan/usa28x.fw keyspan/usa49w.fw keyspan/usa49wlc.fw
endif
fw-shipped-$(CONFIG_USB_SERIAL_TI) += ti_3410.fw ti_5052.fw \
				      mts_cdma.fw mts_gsm.fw mts_edge.fw
fw-shipped-$(CONFIG_USB_SERIAL_EDGEPORT) += edgeport/boot.fw edgeport/boot2.fw \
					    edgeport/down.fw edgeport/down2.fw
fw-shipped-$(CONFIG_USB_SERIAL_EDGEPORT_TI) += edgeport/down3.bin
fw-shipped-$(CONFIG_USB_SERIAL_WHITEHEAT) += whiteheat_loader.fw whiteheat.fw \
					   # whiteheat_loader_debug.fw
fw-shipped-$(CONFIG_USB_SERIAL_KEYSPAN_PDA) += keyspan_pda/keyspan_pda.fw
fw-shipped-$(CONFIG_USB_SERIAL_XIRCOM) += keyspan_pda/xircom_pgs.fw
fw-shipped-$(CONFIG_USB_VICAM) += vicam/firmware.fw
fw-shipped-$(CONFIG_VIDEO_CPIA2) += cpia2/stv0672_vp4.bin

fw-shipped-all := $(fw-shipped-y) $(fw-shipped-m) $(fw-shipped-)

# Directories which we _might_ need to create, so we have a rule for them.
firmware-dirs := $(sort $(patsubst %,$(objtree)/$(obj)/%/,$(dir $(fw-external-y) $(fw-shipped-all))))

quiet_cmd_mkdir = MKDIR   $(patsubst $(objtree)/%,%,$@)
      cmd_mkdir = mkdir -p $@

quiet_cmd_ihex  = IHEX    $@
      cmd_ihex  = $(OBJCOPY) -Iihex -Obinary $< $@

quiet_cmd_ihex2fw  = IHEX2FW $@
      cmd_ihex2fw  = $(objtree)/scripts/ihex2fw $< $@

quiet_cmd_h16tofw  = H16TOFW $@
      cmd_h16tofw  = $(objtree)/scripts/ihex2fw -w $< $@

quiet_cmd_fwbin = MK_FW   $@
      cmd_fwbin = FWNAME="$(patsubst firmware/%.gen.S,%,$@)";		     \
		  FWSTR="$(subst /,_,$(subst .,_,$(subst -,_,$(patsubst	     \
				firmware/%.gen.S,%,$@))))";		     \
		  ASM_WORD=$(if $(CONFIG_64BIT),.quad,.long);		     \
		  ASM_ALIGN=$(if $(CONFIG_64BIT),3,2);			     \
		  PROGBITS=$(if $(CONFIG_ARM),%,@)progbits;		     \
		  echo "/* Generated by firmware/Makefile */"		> $@;\
		  echo "    .section .rodata"				>>$@;\
		  echo "    .p2align $${ASM_ALIGN}"			>>$@;\
		  echo "_fw_$${FWSTR}_bin:"				>>$@;\
		  echo "    .incbin \"$(2)\""				>>$@;\
		  echo "_fw_end:"					>>$@;\
		  echo "   .section .rodata.str,\"aMS\",$${PROGBITS},1"	>>$@;\
		  echo "    .p2align $${ASM_ALIGN}"			>>$@;\
		  echo "_fw_$${FWSTR}_name:"				>>$@;\
		  echo "    .string \"$$FWNAME\""			>>$@;\
		  echo "    .section .builtin_fw,\"a\",$${PROGBITS}"	>>$@;\
		  echo "    .p2align $${ASM_ALIGN}"			>>$@;\
		  echo "    $${ASM_WORD} _fw_$${FWSTR}_name"		>>$@;\
		  echo "    $${ASM_WORD} _fw_$${FWSTR}_bin"		>>$@;\
		  echo "    $${ASM_WORD} _fw_end - _fw_$${FWSTR}_bin"	>>$@;

# One of these files will change, or come into existence, whenever
# the configuration changes between 32-bit and 64-bit. The .S files
# need to change when that happens.
wordsize_deps := $(wildcard include/config/64bit.h include/config/32bit.h \
		include/config/ppc32.h include/config/ppc64.h \
		include/config/superh32.h include/config/superh64.h \
		include/config/x86_32.h include/config/x86_64.h)

# Workaround for make < 3.81, where .SECONDEXPANSION doesn't work.
# It'll end up depending on these targets, so make them a PHONY rule which
# depends on _all_ the directories in $(firmware-dirs), and it'll work out OK.
PHONY += $(objtree)/$$(%) $(objtree)/$(obj)/$$(%)
$(objtree)/$$(%) $(objtree)/$(obj)/$$(%): $(firmware-dirs)
	@true

# For the $$(dir %) trick, where we need % to be expanded first.
.SECONDEXPANSION:

$(patsubst %,$(obj)/%.gen.S, $(fw-shipped-y)): %: $(wordsize_deps) \
		| $(objtree)/$$(dir %)
	$(call cmd,fwbin,$(patsubst %.gen.S,%,$@))
$(patsubst %,$(obj)/%.gen.S, $(fw-external-y)): %: $(wordsize_deps) \
		include/config/extra/firmware/dir.h | $(objtree)/$$(dir %)
	$(call cmd,fwbin,$(fwabs)/$(patsubst $(obj)/%.gen.S,%,$@))

# The .o files depend on the binaries directly; the .S files don't.
$(patsubst %,$(obj)/%.gen.o, $(fw-shipped-y)): %.gen.o: %
$(patsubst %,$(obj)/%.gen.o, $(fw-external-y)): $(obj)/%.gen.o: $(fwdir)/%

# .ihex is used just as a simple way to hold binary files in a source tree
# where binaries are frowned upon. They are directly converted with objcopy.
$(obj)/%: $(obj)/%.ihex | $(objtree)/$(obj)/$$(dir %)
	$(call cmd,ihex)

# .HEX is also Intel HEX, but where the offset and length in each record
# is actually meaningful, because the firmware has to be loaded in a certain
# order rather than as a single binary blob. Thus, we convert them into our
# more compact binary representation of ihex records (<linux/ihex.h>)
$(obj)/%.fw: $(obj)/%.HEX  | $(objtree)/$(obj)/$$(dir %)
	$(call cmd,ihex2fw)

# .H16 is our own modified form of Intel HEX, with 16-bit length for records.
$(obj)/%.fw: $(obj)/%.H16 | $(objtree)/$(obj)/$$(dir %)
	$(call cmd,h16tofw)

$(firmware-dirs):
	$(call cmd,mkdir)

obj-y				 += $(patsubst %,%.gen.o, $(fw-external-y))
obj-$(CONFIG_FIRMWARE_IN_KERNEL) += $(patsubst %,%.gen.o, $(fw-shipped-y))

# Remove .S files and binaries created from ihex
# (during 'make clean' .config isn't included so they're all in $(fw-shipped-))
targets := $(fw-shipped-) $(patsubst $(obj)/%,%, \
				$(shell find $(obj) -name \*.gen.S 2>/dev/null))

# Without this, built-in.o won't be created when it's empty, and the
# final vmlinux link will fail.
obj-n := dummy
