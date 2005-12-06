ifdef CONFIG_INFINIBAND_MTHCA_DEBUG
EXTRA_CFLAGS += -DDEBUG
endif

obj-$(CONFIG_INFINIBAND_MTHCA) += ib_mthca.o

ib_mthca-y :=	mthca_main.o mthca_cmd.o mthca_profile.o mthca_reset.o \
		mthca_allocator.o mthca_eq.o mthca_pd.o mthca_cq.o \
		mthca_mr.o mthca_qp.o mthca_av.o mthca_mcg.o mthca_mad.o \
		mthca_provider.o mthca_memfree.o mthca_uar.o mthca_srq.o \
		mthca_catas.o
