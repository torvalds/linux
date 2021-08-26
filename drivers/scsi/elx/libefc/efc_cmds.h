/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#ifndef __EFC_CMDS_H__
#define __EFC_CMDS_H__

#define EFC_SPARAM_DMA_SZ	112
int
efc_cmd_nport_alloc(struct efc *efc, struct efc_nport *nport,
		    struct efc_domain *domain, u8 *wwpn);
int
efc_cmd_nport_attach(struct efc *efc, struct efc_nport *nport, u32 fc_id);
int
efc_cmd_nport_free(struct efc *efc, struct efc_nport *nport);
int
efc_cmd_domain_alloc(struct efc *efc, struct efc_domain *domain, u32 fcf);
int
efc_cmd_domain_attach(struct efc *efc, struct efc_domain *domain, u32 fc_id);
int
efc_cmd_domain_free(struct efc *efc, struct efc_domain *domain);
int
efc_cmd_node_detach(struct efc *efc, struct efc_remote_node *rnode);
int
efc_node_free_resources(struct efc *efc, struct efc_remote_node *rnode);
int
efc_cmd_node_attach(struct efc *efc, struct efc_remote_node *rnode,
		    struct efc_dma *sparms);
int
efc_cmd_node_alloc(struct efc *efc, struct efc_remote_node *rnode, u32 fc_addr,
		   struct efc_nport *nport);

#endif /* __EFC_CMDS_H */
