ifneq '$(CONFIG_H8300_BUILTIN_DTB)' '""'
BUILTIN_DTB := $(patsubst "%",%,$(CONFIG_H8300_BUILTIN_DTB)).dtb.o
endif

obj-y += $(BUILTIN_DTB)

dtb-$(CONFIG_H8300H_SIM) := h8300h_sim.dtb
dtb-$(CONFIG_H8S_SIM) := h8s_sim.dtb
dtb-$(CONFIG_H8S_EDOSK2674) := edosk2674.dtb

always	    := $(dtb-y)
clean-files := *.dtb.S *.dtb
