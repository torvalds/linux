# Built-in dtb
builtindtb-y		:= nsim_700

ifneq ($(CONFIG_ARC_BUILTIN_DTB_NAME),"")
	builtindtb-y	:= $(patsubst "%",%,$(CONFIG_ARC_BUILTIN_DTB_NAME))
endif

obj-y   += $(builtindtb-y).dtb.o
dtb-y := $(builtindtb-y).dtb

.SECONDARY: $(obj)/$(builtindtb-y).dtb.S


always := $(dtb-y)
clean-files := *.dtb  *.dtb.S
