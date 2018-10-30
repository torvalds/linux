dtstree	:= $(srctree)/$(src)

dtb-y := $(patsubst $(dtstree)/%.dts,%.dtb, $(wildcard $(dtstree)/*.dts))

always += $(dtb-y)
clean-files += *.dtb *.dtb.S
