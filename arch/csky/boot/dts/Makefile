dtstree	:= $(srctree)/$(src)

ifneq '$(CONFIG_CSKY_BUILTIN_DTB)' '""'
builtindtb-y := $(patsubst "%",%,$(CONFIG_CSKY_BUILTIN_DTB))
dtb-y += $(builtindtb-y).dtb
obj-y += $(builtindtb-y).dtb.o
.SECONDARY: $(obj)/$(builtindtb-y).dtb.S
else
dtb-y := $(patsubst $(dtstree)/%.dts,%.dtb, $(wildcard $(dtstree)/*.dts))
endif

always += $(dtb-y)
clean-files += *.dtb *.dtb.S
