dtb-y	+= skeleton.dtb

# Built-in dtb
builtindtb-y				:= skeleton

ifneq ($(CONFIG_METAG_BUILTIN_DTB_NAME),"")
	builtindtb-y			:= $(CONFIG_METAG_BUILTIN_DTB_NAME)
endif
obj-$(CONFIG_METAG_BUILTIN_DTB)	+= $(patsubst "%",%,$(builtindtb-y)).dtb.o

targets	+= dtbs
targets	+= $(dtb-y)

dtbs: $(addprefix $(obj)/, $(dtb-y))

clean-files += *.dtb
