.. SPDX-License-Identifier: GPL-2.0
.. Copyright (C) 2023 Google LLC

===========================================
netns_ipv4 struct fast path usage breakdown
===========================================

=============================== ============================================ =================== =================== =================================================
Type                            Name                                         fastpath_tx_access  fastpath_rx_access  comment
=============================== ============================================ =================== =================== =================================================
struct_inet_timewait_death_row  tcp_death_row
struct_udp_table*               udp_table
struct_ctl_table_header*        forw_hdr
struct_ctl_table_header*        frags_hdr
struct_ctl_table_header*        ipv4_hdr
struct_ctl_table_header*        route_hdr
struct_ctl_table_header*        xfrm4_hdr
struct_ipv4_devconf*            devconf_all
struct_ipv4_devconf*            devconf_dflt
struct_ip_ra_chain              ra_chain
struct_mutex                    ra_mutex
struct_fib_rules_ops*           rules_ops
struct_fib_table                fib_main
struct_fib_table                fib_default
unsigned_int                    fib_rules_require_fldissect
bool                            fib_has_custom_rules
bool                            fib_has_custom_local_routes
bool                            fib_offload_disabled
atomic_t                        fib_num_tclassid_users
struct_hlist_head*              fib_table_hash
struct_sock*                    fibnl
struct_sock*                    mc_autojoin_sk
struct_inet_peer_base*          peers
struct_fqdir*                   fqdir
u8                              sysctl_icmp_echo_ignore_all
u8                              sysctl_icmp_echo_enable_probe
u8                              sysctl_icmp_echo_ignore_broadcasts
u8                              sysctl_icmp_ignore_bogus_error_responses
u8                              sysctl_icmp_errors_use_inbound_ifaddr
int                             sysctl_icmp_ratelimit
int                             sysctl_icmp_ratemask
u32                             ip_rt_min_pmtu
int                             ip_rt_mtu_expires
int                             ip_rt_min_advmss
struct_local_ports              ip_local_ports
u8                              sysctl_tcp_ecn
u8                              sysctl_tcp_ecn_fallback
u8                              sysctl_ip_default_ttl                                                                ip4_dst_hoplimit/ip_select_ttl
u8                              sysctl_ip_no_pmtu_disc
u8                              sysctl_ip_fwd_use_pmtu                       read_mostly                             ip_dst_mtu_maybe_forward/ip_skb_dst_mtu
u8                              sysctl_ip_fwd_update_priority                                                        ip_forward
u8                              sysctl_ip_nonlocal_bind
u8                              sysctl_ip_autobind_reuse
u8                              sysctl_ip_dynaddr
u8                              sysctl_ip_early_demux                                            read_mostly         ip(6)_rcv_finish_core
u8                              sysctl_raw_l3mdev_accept
u8                              sysctl_tcp_early_demux                                           read_mostly         ip(6)_rcv_finish_core
u8                              sysctl_udp_early_demux
u8                              sysctl_nexthop_compat_mode
u8                              sysctl_fwmark_reflect
u8                              sysctl_tcp_fwmark_accept
u8                              sysctl_tcp_l3mdev_accept                                         read_mostly         __inet6_lookup_established/inet_request_bound_dev_if
u8                              sysctl_tcp_mtu_probing
int                             sysctl_tcp_mtu_probe_floor
int                             sysctl_tcp_base_mss
int                             sysctl_tcp_min_snd_mss                       read_mostly                             __tcp_mtu_to_mss(tcp_write_xmit)
int                             sysctl_tcp_probe_threshold                                                           tcp_mtu_probe(tcp_write_xmit)
u32                             sysctl_tcp_probe_interval                                                            tcp_mtu_check_reprobe(tcp_write_xmit)
int                             sysctl_tcp_keepalive_time
int                             sysctl_tcp_keepalive_intvl
u8                              sysctl_tcp_keepalive_probes
u8                              sysctl_tcp_syn_retries
u8                              sysctl_tcp_synack_retries
u8                              sysctl_tcp_syncookies                                                                generated_on_syn
u8                              sysctl_tcp_migrate_req                                                               reuseport
u8                              sysctl_tcp_comp_sack_nr                                                              __tcp_ack_snd_check
int                             sysctl_tcp_reordering                                            read_mostly         tcp_may_raise_cwnd/tcp_cong_control
u8                              sysctl_tcp_retries1
u8                              sysctl_tcp_retries2
u8                              sysctl_tcp_orphan_retries
u8                              sysctl_tcp_tw_reuse                                                                  timewait_sock_ops
unsigned_int                    sysctl_tcp_tw_reuse_delay                                                            timewait_sock_ops
int                             sysctl_tcp_fin_timeout                                                               TCP_LAST_ACK/tcp_rcv_state_process
unsigned_int                    sysctl_tcp_notsent_lowat                     read_mostly                             tcp_notsent_lowat/tcp_stream_memory_free
u8                              sysctl_tcp_sack                                                                      tcp_syn_options
u8                              sysctl_tcp_window_scaling                                                            tcp_syn_options,tcp_parse_options
u8                              sysctl_tcp_timestamps
u8                              sysctl_tcp_early_retrans                     read_mostly                             tcp_schedule_loss_probe(tcp_write_xmit)
u8                              sysctl_tcp_recovery                                                                  tcp_fastretrans_alert
u8                              sysctl_tcp_thin_linear_timeouts                                                      tcp_retrans_timer(on_thin_streams)
u8                              sysctl_tcp_slow_start_after_idle                                                     unlikely(tcp_cwnd_validate-network-not-starved)
u8                              sysctl_tcp_retrans_collapse
u8                              sysctl_tcp_stdurg                                                                    unlikely(tcp_check_urg)
u8                              sysctl_tcp_rfc1337
u8                              sysctl_tcp_abort_on_overflow
u8                              sysctl_tcp_fack
int                             sysctl_tcp_max_reordering                                                            tcp_check_sack_reordering
int                             sysctl_tcp_adv_win_scale                                                             tcp_init_buffer_space
u8                              sysctl_tcp_dsack                                                                     partial_packet_or_retrans_in_tcp_data_queue
u8                              sysctl_tcp_app_win                                                                   tcp_win_from_space
u8                              sysctl_tcp_frto                                                                      tcp_enter_loss
u8                              sysctl_tcp_nometrics_save                                                            TCP_LAST_ACK/tcp_update_metrics
u8                              sysctl_tcp_no_ssthresh_metrics_save                                                  TCP_LAST_ACK/tcp_(update/init)_metrics
u8                              sysctl_tcp_moderate_rcvbuf                   read_mostly         read_mostly         tcp_tso_should_defer(tx);tcp_rcv_space_adjust(rx)
u8                              sysctl_tcp_tso_win_divisor                   read_mostly                             tcp_tso_should_defer(tcp_write_xmit)
u8                              sysctl_tcp_workaround_signed_windows                                                 tcp_select_window
int                             sysctl_tcp_limit_output_bytes                read_mostly                             tcp_small_queue_check(tcp_write_xmit)
int                             sysctl_tcp_challenge_ack_limit
int                             sysctl_tcp_min_rtt_wlen                      read_mostly                             tcp_ack_update_rtt
u8                              sysctl_tcp_min_tso_segs                                                              unlikely(icsk_ca_ops-written)
u8                              sysctl_tcp_tso_rtt_log                       read_mostly                             tcp_tso_autosize
u8                              sysctl_tcp_autocorking                       read_mostly                             tcp_push/tcp_should_autocork
u8                              sysctl_tcp_reflect_tos                                                               tcp_v(4/6)_send_synack
int                             sysctl_tcp_invalid_ratelimit
int                             sysctl_tcp_pacing_ss_ratio                                                           default_cong_cont(tcp_update_pacing_rate)
int                             sysctl_tcp_pacing_ca_ratio                                                           default_cong_cont(tcp_update_pacing_rate)
int                             sysctl_tcp_wmem[3]                           read_mostly                             tcp_wmem_schedule(sendmsg/sendpage)
int                             sysctl_tcp_rmem[3]                                               read_mostly         __tcp_grow_window(tx),tcp_rcv_space_adjust(rx)
unsigned_int                    sysctl_tcp_child_ehash_entries
unsigned_long                   sysctl_tcp_comp_sack_delay_ns                                                        __tcp_ack_snd_check
unsigned_long                   sysctl_tcp_comp_sack_slack_ns                                                        __tcp_ack_snd_check
int                             sysctl_max_syn_backlog
int                             sysctl_tcp_fastopen
struct_tcp_congestion_ops       tcp_congestion_control                                                               init_cc
struct_tcp_fastopen_context     tcp_fastopen_ctx
unsigned_int                    sysctl_tcp_fastopen_blackhole_timeout
atomic_t                        tfo_active_disable_times
unsigned_long                   tfo_active_disable_stamp
u32                             tcp_challenge_timestamp
u32                             tcp_challenge_count
u8                              sysctl_tcp_plb_enabled
u8                              sysctl_tcp_plb_idle_rehash_rounds
u8                              sysctl_tcp_plb_rehash_rounds
u8                              sysctl_tcp_plb_suspend_rto_sec
int                             sysctl_tcp_plb_cong_thresh
int                             sysctl_udp_wmem_min
int                             sysctl_udp_rmem_min
u8                              sysctl_fib_notify_on_flag_change
u8                              sysctl_udp_l3mdev_accept
u8                              sysctl_igmp_llm_reports
int                             sysctl_igmp_max_memberships
int                             sysctl_igmp_max_msf
int                             sysctl_igmp_qrv
struct_ping_group_range         ping_group_range
atomic_t                        dev_addr_genid
unsigned_int                    sysctl_udp_child_hash_entries
unsigned_long*                  sysctl_local_reserved_ports
int                             sysctl_ip_prot_sock
struct_mr_table*                mrt
struct_list_head                mr_tables
struct_fib_rules_ops*           mr_rules_ops
u32                             sysctl_fib_multipath_hash_fields
u8                              sysctl_fib_multipath_use_neigh
u8                              sysctl_fib_multipath_hash_policy
struct_fib_notifier_ops*        notifier_ops
unsigned_int                    fib_seq
struct_fib_notifier_ops*        ipmr_notifier_ops
unsigned_int                    ipmr_seq
atomic_t                        rt_genid
siphash_key_t                   ip_id_key
=============================== ============================================ =================== =================== =================================================
