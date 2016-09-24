obj-$(CONFIG_CRYPTO_DEV_VMX_ENCRYPT) += vmx-crypto.o
vmx-crypto-objs := vmx.o aesp8-ppc.o ghashp8-ppc.o aes.o aes_cbc.o aes_ctr.o aes_xts.o ghash.o

ifeq ($(CONFIG_CPU_LITTLE_ENDIAN),y)
TARGET := linux-ppc64le
else
TARGET := linux-ppc64
endif

quiet_cmd_perl = PERL $@
      cmd_perl = $(PERL) $(<) $(TARGET) > $(@)

$(src)/aesp8-ppc.S: $(src)/aesp8-ppc.pl
	$(call cmd,perl)
  
$(src)/ghashp8-ppc.S: $(src)/ghashp8-ppc.pl
	$(call cmd,perl)

.PRECIOUS: $(obj)/aesp8-ppc.S $(obj)/ghashp8-ppc.S
