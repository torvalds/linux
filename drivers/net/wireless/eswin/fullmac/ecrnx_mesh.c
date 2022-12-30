/**
 ****************************************************************************************
 *
 * @file ecrnx_mesh.c
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */

/**
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "ecrnx_mesh.h"

/**
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */

struct ecrnx_mesh_proxy *ecrnx_get_mesh_proxy_info(struct ecrnx_vif *p_ecrnx_vif, u8 *p_sta_addr, bool local)
{
    struct ecrnx_mesh_proxy *p_mesh_proxy = NULL;
    struct ecrnx_mesh_proxy *p_cur_proxy;

    /* Look for proxied devices with provided address */
    list_for_each_entry(p_cur_proxy, &p_ecrnx_vif->ap.proxy_list, list) {
        if (p_cur_proxy->local != local) {
            continue;
        }

        if (!memcmp(&p_cur_proxy->ext_sta_addr, p_sta_addr, ETH_ALEN)) {
            p_mesh_proxy = p_cur_proxy;
            break;
        }
    }

    /* Return the found information */
    return p_mesh_proxy;
}
