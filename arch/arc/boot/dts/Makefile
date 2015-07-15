# Built-in dtb
builtindtb-y		:= nsim_700

ifneq ($(CONFIG_ARC_BUILTIN_DTB_NAME),"")
	builtindtb-y	:= $(patsubst "%",%,$(CONFIG_ARC_BUILTIN_DTB_NAME))
endif

obj-y   += $(builtindtb-y).dtb.o
targets += $(builtindtb-y).dtb

.SECONDARY: $(obj)/$(builtindtb-y).dtb.S

dtbs:  $(addprefix  $(obj)/, $(builtindtb-y).dtb)

clean-files := *.dtb  *.dtb.S
