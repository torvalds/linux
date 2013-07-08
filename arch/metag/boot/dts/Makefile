dtb-y	+= skeleton.dtb
dtb-y	+= tz1090_generic.dtb

# Built-in dtb
builtindtb-y				:= skeleton
builtindtb-$(CONFIG_SOC_TZ1090)		:= tz1090_generic

ifneq ($(CONFIG_METAG_BUILTIN_DTB_NAME),"")
	builtindtb-y			:= $(patsubst "%",%,$(CONFIG_METAG_BUILTIN_DTB_NAME))
endif

dtb-$(CONFIG_METAG_BUILTIN_DTB)	+= $(builtindtb-y).dtb
obj-$(CONFIG_METAG_BUILTIN_DTB)	+= $(builtindtb-y).dtb.o

targets	+= dtbs
targets	+= $(dtb-y)

.SECONDARY: $(obj)/$(builtindtb-y).dtb.S

dtbs: $(addprefix $(obj)/, $(dtb-y))

clean-files += *.dtb *.dtb.S
