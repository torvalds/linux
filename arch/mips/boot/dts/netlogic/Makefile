dtb-$(CONFIG_DT_XLP_EVP)	+= xlp_evp.dtb
dtb-$(CONFIG_DT_XLP_SVP)	+= xlp_svp.dtb
dtb-$(CONFIG_DT_XLP_FVP)	+= xlp_fvp.dtb
dtb-$(CONFIG_DT_XLP_GVP)	+= xlp_gvp.dtb
dtb-$(CONFIG_DT_XLP_RVP)	+= xlp_rvp.dtb

obj-y				+= $(patsubst %.dtb, %.dtb.o, $(dtb-y))

# Force kbuild to make empty built-in.o if necessary
obj-				+= dummy.o

always				:= $(dtb-y)
clean-files			:= *.dtb *.dtb.S
