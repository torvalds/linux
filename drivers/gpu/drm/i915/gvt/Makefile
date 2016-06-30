GVT_DIR := gvt
GVT_SOURCE := gvt.o aperture_gm.o handlers.o firmware.o

ccflags-y                      += -I$(src) -I$(src)/$(GVT_DIR) -Wall
i915-y			       += $(addprefix $(GVT_DIR)/, $(GVT_SOURCE))
