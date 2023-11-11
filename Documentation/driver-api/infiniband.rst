===========================================
InfiniBand and Remote DMA (RDMA) Interfaces
===========================================

Introduction and Overview
=========================

TBD

InfiniBand core interfaces
==========================

.. kernel-doc:: drivers/infiniband/core/iwpm_util.h
    :internal:

.. kernel-doc:: drivers/infiniband/core/cq.c
    :export:

.. kernel-doc:: drivers/infiniband/core/cm.c
    :export:

.. kernel-doc:: drivers/infiniband/core/rw.c
    :export:

.. kernel-doc:: drivers/infiniband/core/device.c
    :export:

.. kernel-doc:: drivers/infiniband/core/verbs.c
    :export:

.. kernel-doc:: drivers/infiniband/core/packer.c
    :export:

.. kernel-doc:: drivers/infiniband/core/sa_query.c
    :export:

.. kernel-doc:: drivers/infiniband/core/ud_header.c
    :export:

.. kernel-doc:: drivers/infiniband/core/umem.c
    :export:

.. kernel-doc:: drivers/infiniband/core/umem_odp.c
    :export:

RDMA Verbs transport library
============================

.. kernel-doc:: drivers/infiniband/sw/rdmavt/mr.c
    :export:

.. kernel-doc:: drivers/infiniband/sw/rdmavt/rc.c
    :export:

.. kernel-doc:: drivers/infiniband/sw/rdmavt/ah.c
    :export:

.. kernel-doc:: drivers/infiniband/sw/rdmavt/vt.c
    :export:

.. kernel-doc:: drivers/infiniband/sw/rdmavt/cq.c
    :export:

.. kernel-doc:: drivers/infiniband/sw/rdmavt/qp.c
    :export:

.. kernel-doc:: drivers/infiniband/sw/rdmavt/mcast.c
    :export:

Upper Layer Protocols
=====================

iSCSI Extensions for RDMA (iSER)
--------------------------------

.. kernel-doc:: drivers/infiniband/ulp/iser/iscsi_iser.h
   :internal:

.. kernel-doc:: drivers/infiniband/ulp/iser/iscsi_iser.c
   :functions: iscsi_iser_pdu_alloc iser_initialize_task_headers \
	iscsi_iser_task_init iscsi_iser_mtask_xmit iscsi_iser_task_xmit \
	iscsi_iser_cleanup_task iscsi_iser_check_protection \
	iscsi_iser_conn_create iscsi_iser_conn_bind \
	iscsi_iser_conn_start iscsi_iser_conn_stop \
	iscsi_iser_session_destroy iscsi_iser_session_create \
	iscsi_iser_set_param iscsi_iser_ep_connect iscsi_iser_ep_poll \
	iscsi_iser_ep_disconnect

.. kernel-doc:: drivers/infiniband/ulp/iser/iser_initiator.c
   :internal:

.. kernel-doc:: drivers/infiniband/ulp/iser/iser_verbs.c
   :internal:

Omni-Path (OPA) Virtual NIC support
-----------------------------------

.. kernel-doc:: drivers/infiniband/ulp/opa_vnic/opa_vnic_internal.h
   :internal:

.. kernel-doc:: drivers/infiniband/ulp/opa_vnic/opa_vnic_encap.h
   :internal:

.. kernel-doc:: drivers/infiniband/ulp/opa_vnic/opa_vnic_vema_iface.c
   :internal:

.. kernel-doc:: drivers/infiniband/ulp/opa_vnic/opa_vnic_vema.c
   :internal:

InfiniBand SCSI RDMA protocol target support
--------------------------------------------

.. kernel-doc:: drivers/infiniband/ulp/srpt/ib_srpt.h
   :internal:

.. kernel-doc:: drivers/infiniband/ulp/srpt/ib_srpt.c
   :internal:

iSCSI Extensions for RDMA (iSER) target support
-----------------------------------------------

.. kernel-doc:: drivers/infiniband/ulp/isert/ib_isert.c
   :internal:

