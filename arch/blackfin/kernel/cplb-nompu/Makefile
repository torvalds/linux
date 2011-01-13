#
# arch/blackfin/kernel/cplb-nompu/Makefile
#

obj-y := cplbinit.o cplbmgr.o

CFLAGS_cplbmgr.o := -ffixed-I0 -ffixed-I1 -ffixed-I2 -ffixed-I3 \
		    -ffixed-L0 -ffixed-L1 -ffixed-L2 -ffixed-L3 \
		    -ffixed-M0 -ffixed-M1 -ffixed-M2 -ffixed-M3 \
		    -ffixed-B0 -ffixed-B1 -ffixed-B2 -ffixed-B3
