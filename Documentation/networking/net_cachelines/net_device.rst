.. SPDX-License-Identifier: GPL-2.0
.. Copyright (C) 2023 Google LLC

===========================================
net_device struct fast path usage breakdown
===========================================

Type                                Name                    fastpath_tx_access  fastpath_rx_access  Comments
..struct                            ..net_device                                                    
char                                name[16]                -                   -                   
struct_netdev_name_node*            name_node                                                       
struct_dev_ifalias*                 ifalias                                                         
unsigned_long                       mem_end                                                         
unsigned_long                       mem_start                                                       
unsigned_long                       base_addr                                                       
unsigned_long                       state                   read_mostly         read_mostly         netif_running(dev)
struct_list_head                    dev_list                                                        
struct_list_head                    napi_list                                                       
struct_list_head                    unreg_list                                                      
struct_list_head                    close_list                                                      
struct_list_head                    ptype_all               read_mostly         -                   dev_nit_active(tx)
struct_list_head                    ptype_specific                              read_mostly         deliver_ptype_list_skb/__netif_receive_skb_core(rx)
struct                              adj_list                                                        
unsigned_int                        flags                   read_mostly         read_mostly         __dev_queue_xmit,__dev_xmit_skb,ip6_output,__ip6_finish_output(tx);ip6_rcv_core(rx)
xdp_features_t                      xdp_features                                                    
unsigned_long_long                  priv_flags              read_mostly         -                   __dev_queue_xmit(tx)
struct_net_device_ops*              netdev_ops              read_mostly         -                   netdev_core_pick_tx,netdev_start_xmit(tx)
struct_xdp_metadata_ops*            xdp_metadata_ops                                                
int                                 ifindex                 -                   read_mostly         ip6_rcv_core
unsigned_short                      gflags                                                          
unsigned_short                      hard_header_len         read_mostly         read_mostly         ip6_xmit(tx);gro_list_prepare(rx)
unsigned_int                        mtu                     read_mostly         -                   ip_finish_output2
unsigned_short                      needed_headroom         read_mostly         -                   LL_RESERVED_SPACE/ip_finish_output2
unsigned_short                      needed_tailroom                                                 
netdev_features_t                   features                read_mostly         read_mostly         HARD_TX_LOCK,netif_skb_features,sk_setup_caps(tx);netif_elide_gro(rx)
netdev_features_t                   hw_features                                                     
netdev_features_t                   wanted_features                                                 
netdev_features_t                   vlan_features                                                   
netdev_features_t                   hw_enc_features         -                   -                   netif_skb_features
netdev_features_t                   mpls_features                                                   
netdev_features_t                   gso_partial_features    read_mostly                             gso_features_check
unsigned_int                        min_mtu                                                         
unsigned_int                        max_mtu                                                         
unsigned_short                      type                                                            
unsigned_char                       min_header_len                                                  
unsigned_char                       name_assign_type                                                
int                                 group                                                           
struct_net_device_stats             stats                                                           
struct_net_device_core_stats*       core_stats                                                      
atomic_t                            carrier_up_count                                                
atomic_t                            carrier_down_count                                              
struct_iw_handler_def*              wireless_handlers                                               
struct_iw_public_data*              wireless_data                                                   
struct_ethtool_ops*                 ethtool_ops                                                     
struct_l3mdev_ops*                  l3mdev_ops                                                      
struct_ndisc_ops*                   ndisc_ops                                                       
struct_xfrmdev_ops*                 xfrmdev_ops                                                     
struct_tlsdev_ops*                  tlsdev_ops                                                      
struct_header_ops*                  header_ops              read_mostly         -                   ip_finish_output2,ip6_finish_output2(tx)
unsigned_char                       operstate                                                       
unsigned_char                       link_mode                                                       
unsigned_char                       if_port                                                         
unsigned_char                       dma                                                             
unsigned_char                       perm_addr[32]                                                   
unsigned_char                       addr_assign_type                                                
unsigned_char                       addr_len                                                        
unsigned_char                       upper_level                                                     
unsigned_char                       lower_level                                                     
unsigned_short                      neigh_priv_len                                                  
unsigned_short                      padded                                                          
unsigned_short                      dev_id                                                          
unsigned_short                      dev_port                                                        
spinlock_t                          addr_list_lock                                                  
int                                 irq                                                             
struct_netdev_hw_addr_list          uc                                                              
struct_netdev_hw_addr_list          mc                                                              
struct_netdev_hw_addr_list          dev_addrs                                                       
struct_kset*                        queues_kset                                                     
struct_list_head                    unlink_list                                                     
unsigned_int                        promiscuity                                                     
unsigned_int                        allmulti                                                        
bool                                uc_promisc                                                      
unsigned_char                       nested_level                                                    
struct_in_device*                   ip_ptr                  read_mostly         read_mostly         __in_dev_get
struct_inet6_dev*                   ip6_ptr                 read_mostly         read_mostly         __in6_dev_get
struct_vlan_info*                   vlan_info                                                       
struct_dsa_port*                    dsa_ptr                                                         
struct_tipc_bearer*                 tipc_ptr                                                        
void*                               atalk_ptr                                                       
void*                               ax25_ptr                                                        
struct_wireless_dev*                ieee80211_ptr                                                   
struct_wpan_dev*                    ieee802154_ptr                                                  
struct_mpls_dev*                    mpls_ptr                                                        
struct_mctp_dev*                    mctp_ptr                                                        
unsigned_char*                      dev_addr                                                        
struct_netdev_queue*                _rx                     read_mostly         -                   netdev_get_rx_queue(rx)
unsigned_int                        num_rx_queues                                                   
unsigned_int                        real_num_rx_queues      -                   read_mostly         get_rps_cpu
struct_bpf_prog*                    xdp_prog                -                   read_mostly         netif_elide_gro()
unsigned_long                       gro_flush_timeout       -                   read_mostly         napi_complete_done
u32                                 napi_defer_hard_irqs    -                   read_mostly         napi_complete_done
unsigned_int                        gro_max_size            -                   read_mostly         skb_gro_receive
unsigned_int                        gro_ipv4_max_size       -                   read_mostly         skb_gro_receive
rx_handler_func_t*                  rx_handler              read_mostly         -                   __netif_receive_skb_core
void*                               rx_handler_data         read_mostly         -                   
struct_netdev_queue*                ingress_queue           read_mostly         -                   
struct_bpf_mprog_entry              tcx_ingress             -                   read_mostly         sch_handle_ingress
struct_nf_hook_entries*             nf_hooks_ingress                                                
unsigned_char                       broadcast[32]                                                   
struct_cpu_rmap*                    rx_cpu_rmap                                                     
struct_hlist_node                   index_hlist                                                     
struct_netdev_queue*                _tx                     read_mostly         -                   netdev_get_tx_queue(tx)
unsigned_int                        num_tx_queues           -                   -                   
unsigned_int                        real_num_tx_queues      read_mostly         -                   skb_tx_hash,netdev_core_pick_tx(tx)
unsigned_int                        tx_queue_len                                                    
spinlock_t                          tx_global_lock                                                  
struct_xdp_dev_bulk_queue__percpu*  xdp_bulkq                                                       
struct_xps_dev_maps*                xps_maps[2]             read_mostly         -                   __netif_set_xps_queue
struct_bpf_mprog_entry              tcx_egress              read_mostly         -                   sch_handle_egress
struct_nf_hook_entries*             nf_hooks_egress         read_mostly         -                   
struct_hlist_head                   qdisc_hash[16]                                                  
struct_timer_list                   watchdog_timer                                                  
int                                 watchdog_timeo                                                  
u32                                 proto_down_reason                                               
struct_list_head                    todo_list                                                       
int__percpu*                        pcpu_refcnt                                                     
refcount_t                          dev_refcnt                                                      
struct_ref_tracker_dir              refcnt_tracker                                                  
struct_list_head                    link_watch_list                                                 
enum:8                              reg_state                                                       
bool                                dismantle                                                       
enum:16                             rtnl_link_state                                                 
bool                                needs_free_netdev                                               
void*priv_destructor                struct_net_device                                               
struct_netpoll_info*                npinfo                  -                   read_mostly         napi_poll/napi_poll_lock
possible_net_t                      nd_net                  -                   read_mostly         (dev_net)napi_busy_loop,tcp_v(4/6)_rcv,ip(v6)_rcv,ip(6)_input,ip(6)_input_finish
void*                               ml_priv                                                         
enum_netdev_ml_priv_type            ml_priv_type                                                    
struct_pcpu_lstats__percpu*         lstats                  read_mostly                             dev_lstats_add()
struct_pcpu_sw_netstats__percpu*    tstats                  read_mostly                             dev_sw_netstats_tx_add()
struct_pcpu_dstats__percpu*         dstats                                                          
struct_garp_port*                   garp_port                                                       
struct_mrp_port*                    mrp_port                                                        
struct_dm_hw_stat_delta*            dm_private                                                      
struct_device                       dev                     -                   -                   
struct_attribute_group*             sysfs_groups[4]                                                 
struct_attribute_group*             sysfs_rx_queue_group                                            
struct_rtnl_link_ops*               rtnl_link_ops                                                   
unsigned_int                        gso_max_size            read_mostly         -                   sk_dst_gso_max_size
unsigned_int                        tso_max_size                                                    
u16                                 gso_max_segs            read_mostly         -                   gso_max_segs
u16                                 tso_max_segs                                                    
unsigned_int                        gso_ipv4_max_size       read_mostly         -                   sk_dst_gso_max_size
struct_dcbnl_rtnl_ops*              dcbnl_ops                                                       
s16                                 num_tc                  read_mostly         -                   skb_tx_hash
struct_netdev_tc_txq                tc_to_txq[16]           read_mostly         -                   skb_tx_hash
u8                                  prio_tc_map[16]                                                 
unsigned_int                        fcoe_ddp_xid                                                    
struct_netprio_map*                 priomap                                                         
struct_phy_device*                  phydev                                                          
struct_sfp_bus*                     sfp_bus                                                         
struct_lock_class_key*              qdisc_tx_busylock                                               
bool                                proto_down                                                      
unsigned:1                          wol_enabled                                                     
unsigned:1                          threaded                -                   -                   napi_poll(napi_enable,dev_set_threaded)
struct_list_head                    net_notifier_list                                               
struct_macsec_ops*                  macsec_ops                                                      
struct_udp_tunnel_nic_info*         udp_tunnel_nic_info                                             
struct_udp_tunnel_nic*              udp_tunnel_nic                                                  
unsigned_int                        xdp_zc_max_segs                                                 
struct_bpf_xdp_entity               xdp_state[3]                                                    
u8                                  dev_addr_shadow[32]                                             
netdevice_tracker                   linkwatch_dev_tracker                                           
netdevice_tracker                   watchdog_dev_tracker                                            
netdevice_tracker                   dev_registered_tracker                                          
struct_rtnl_hw_stats64*             offload_xstats_l3                                               
struct_devlink_port*                devlink_port                                                    
struct_dpll_pin*                    dpll_pin                                                        
