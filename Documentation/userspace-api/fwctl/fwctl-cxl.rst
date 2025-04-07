.. SPDX-License-Identifier: GPL-2.0

================
fwctl cxl driver
================

:Author: Dave Jiang

Overview
========

The CXL spec defines a set of commands that can be issued to the mailbox of a
CXL device or switch. It also left room for vendor specific commands to be
issued to the mailbox as well. fwctl provides a path to issue a set of allowed
mailbox commands from user space to the device moderated by the kernel driver.

The following 3 commands will be used to support CXL Features:
CXL spec r3.1 8.2.9.6.1 Get Supported Features (Opcode 0500h)
CXL spec r3.1 8.2.9.6.2 Get Feature (Opcode 0501h)
CXL spec r3.1 8.2.9.6.3 Set Feature (Opcode 0502h)

The "Get Supported Features" return data may be filtered by the kernel driver to
drop any features that are forbidden by the kernel or being exclusively used by
the kernel. The driver will set the "Set Feature Size" of the "Get Supported
Features Supported Feature Entry" to 0 to indicate that the Feature cannot be
modified. The "Get Supported Features" command and the "Get Features" falls
under the fwctl policy of FWCTL_RPC_CONFIGURATION.

For "Set Feature" command, the access policy currently is broken down into two
categories depending on the Set Feature effects reported by the device. If the
Set Feature will cause immediate change to the device, the fwctl access policy
must be FWCTL_RPC_DEBUG_WRITE_FULL. The effects for this level are
"immediate config change", "immediate data change", "immediate policy change",
or "immediate log change" for the set effects mask. If the effects are "config
change with cold reset" or "config change with conventional reset", then the
fwctl access policy must be FWCTL_RPC_DEBUG_WRITE or higher.

fwctl cxl User API
==================

.. kernel-doc:: include/uapi/fwctl/cxl.h

1. Driver info query
--------------------

First step for the app is to issue the ioctl(FWCTL_CMD_INFO). Successful
invocation of the ioctl implies the Features capability is operational and
returns an all zeros 32bit payload. A ``struct fwctl_info`` needs to be filled
out with the ``fwctl_info.out_device_type`` set to ``FWCTL_DEVICE_TYPE_CXL``.
The return data should be ``struct fwctl_info_cxl`` that contains a reserved
32bit field that should be all zeros.

2. Send hardware commands
-------------------------

Next step is to send the 'Get Supported Features' command to the driver from
user space via ioctl(FWCTL_RPC). A ``struct fwctl_rpc_cxl`` is pointed to
by ``fwctl_rpc.in``. ``struct fwctl_rpc_cxl.in_payload`` points to
the hardware input structure that is defined by the CXL spec. ``fwctl_rpc.out``
points to the buffer that contains a ``struct fwctl_rpc_cxl_out`` that includes
the hardware output data inlined as ``fwctl_rpc_cxl_out.payload``. This command
is called twice. First time to retrieve the number of features supported.
A second time to retrieve the specific feature details as the output data.

After getting the specific feature details, a Get/Set Feature command can be
appropriately programmed and sent. For a "Set Feature" command, the retrieved
feature info contains an effects field that details the resulting
"Set Feature" command will trigger. That will inform the user whether
the system is configured to allowed the "Set Feature" command or not.

Code example of a Get Feature
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

        static int cxl_fwctl_rpc_get_test_feature(int fd, struct test_feature *feat_ctx,
                                                  const uint32_t expected_data)
        {
                struct cxl_mbox_get_feat_in *feat_in;
                struct fwctl_rpc_cxl_out *out;
                struct fwctl_rpc rpc = {0};
                struct fwctl_rpc_cxl *in;
                size_t out_size, in_size;
                uint32_t val;
                void *data;
                int rc;

                in_size = sizeof(*in) + sizeof(*feat_in);
                rc = posix_memalign((void **)&in, 16, in_size);
                if (rc)
                        return -ENOMEM;
                memset(in, 0, in_size);
                feat_in = &in->get_feat_in;

                uuid_copy(feat_in->uuid, feat_ctx->uuid);
                feat_in->count = feat_ctx->get_size;

                out_size = sizeof(*out) + feat_ctx->get_size;
                rc = posix_memalign((void **)&out, 16, out_size);
                if (rc)
                        goto free_in;
                memset(out, 0, out_size);

                in->opcode = CXL_MBOX_OPCODE_GET_FEATURE;
                in->op_size = sizeof(*feat_in);

                rpc.size = sizeof(rpc);
                rpc.scope = FWCTL_RPC_CONFIGURATION;
                rpc.in_len = in_size;
                rpc.out_len = out_size;
                rpc.in = (uint64_t)(uint64_t *)in;
                rpc.out = (uint64_t)(uint64_t *)out;

                rc = send_command(fd, &rpc, out);
                if (rc)
                        goto free_all;

                data = out->payload;
                val = le32toh(*(__le32 *)data);
                if (memcmp(&val, &expected_data, sizeof(val)) != 0) {
                        rc = -ENXIO;
                        goto free_all;
                }

        free_all:
                free(out);
        free_in:
                free(in);
                return rc;
        }

Take a look at CXL CLI test directory
<https://github.com/pmem/ndctl/tree/main/test/fwctl.c> for a detailed user code
for examples on how to exercise this path.


fwctl cxl Kernel API
====================

.. kernel-doc:: drivers/cxl/core/features.c
   :export:
.. kernel-doc:: include/cxl/features.h
