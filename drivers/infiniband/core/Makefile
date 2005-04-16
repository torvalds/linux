EXTRA_CFLAGS += -Idrivers/infiniband/include

obj-$(CONFIG_INFINIBAND) +=	ib_core.o ib_mad.o ib_sa.o ib_umad.o

ib_core-y :=			packer.o ud_header.o verbs.o sysfs.o \
				device.o fmr_pool.o cache.o

ib_mad-y :=			mad.o smi.o agent.o

ib_sa-y :=			sa_query.o

ib_umad-y :=			user_mad.o
