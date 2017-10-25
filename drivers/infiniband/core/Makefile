infiniband-$(CONFIG_INFINIBAND_ADDR_TRANS)	:= rdma_cm.o
user_access-$(CONFIG_INFINIBAND_ADDR_TRANS)	:= rdma_ucm.o

obj-$(CONFIG_INFINIBAND) +=		ib_core.o ib_cm.o iw_cm.o \
					$(infiniband-y)
obj-$(CONFIG_INFINIBAND_USER_MAD) +=	ib_umad.o
obj-$(CONFIG_INFINIBAND_USER_ACCESS) +=	ib_uverbs.o ib_ucm.o \
					$(user_access-y)

ib_core-y :=			packer.o ud_header.o verbs.o cq.o rw.o sysfs.o \
				device.o fmr_pool.o cache.o netlink.o \
				roce_gid_mgmt.o mr_pool.o addr.o sa_query.o \
				multicast.o mad.o smi.o agent.o mad_rmpp.o \
				security.o nldev.o

ib_core-$(CONFIG_INFINIBAND_USER_MEM) += umem.o
ib_core-$(CONFIG_INFINIBAND_ON_DEMAND_PAGING) += umem_odp.o
ib_core-$(CONFIG_CGROUP_RDMA) += cgroup.o

ib_cm-y :=			cm.o

iw_cm-y :=			iwcm.o iwpm_util.o iwpm_msg.o

rdma_cm-y :=			cma.o

rdma_cm-$(CONFIG_INFINIBAND_ADDR_TRANS_CONFIGFS) += cma_configfs.o

rdma_ucm-y :=			ucma.o

ib_umad-y :=			user_mad.o

ib_ucm-y :=			ucm.o

ib_uverbs-y :=			uverbs_main.o uverbs_cmd.o uverbs_marshall.o \
				rdma_core.o uverbs_std_types.o uverbs_ioctl.o \
				uverbs_ioctl_merge.o
