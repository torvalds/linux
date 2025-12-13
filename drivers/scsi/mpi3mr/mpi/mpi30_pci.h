/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright 2016-2023 Broadcom Inc. All rights reserved.
 *
 */
#ifndef MPI30_PCI_H
#define MPI30_PCI_H     1
#ifndef MPI3_NVME_ENCAP_CMD_MAX
#define MPI3_NVME_ENCAP_CMD_MAX               (1)
#endif
#define MPI3_NVME_FLAGS_FORCE_ADMIN_ERR_REPLY_MASK      (0x0002)
#define MPI3_NVME_FLAGS_FORCE_ADMIN_ERR_REPLY_SHIFT     (1)
#define MPI3_NVME_FLAGS_FORCE_ADMIN_ERR_REPLY_FAIL_ONLY (0x0000)
#define MPI3_NVME_FLAGS_FORCE_ADMIN_ERR_REPLY_ALL       (0x0002)
#define MPI3_NVME_FLAGS_SUBMISSIONQ_MASK                (0x0001)
#define MPI3_NVME_FLAGS_SUBMISSIONQ_SHIFT               (0)
#define MPI3_NVME_FLAGS_SUBMISSIONQ_IO                  (0x0000)
#define MPI3_NVME_FLAGS_SUBMISSIONQ_ADMIN               (0x0001)

#endif
