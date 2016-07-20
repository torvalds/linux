GVT_DIR := gvt
GVT_SOURCE := gvt.o aperture_gm.o handlers.o vgpu.o trace_points.o firmware.o \
	interrupt.o gtt.o cfg_space.o opregion.o

ccflags-y                      += -I$(src) -I$(src)/$(GVT_DIR) -Wall
i915-y			       += $(addprefix $(GVT_DIR)/, $(GVT_SOURCE))
