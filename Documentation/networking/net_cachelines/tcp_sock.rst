.. SPDX-License-Identifier: GPL-2.0
.. Copyright (C) 2023 Google LLC

=========================================
tcp_sock struct fast path usage breakdown
=========================================

============================= ======================= =================== =================== ==================================================================================================================================================================================================================
Type                          Name                    fastpath_tx_access  fastpath_rx_access  Comments
============================= ======================= =================== =================== ==================================================================================================================================================================================================================
struct inet_connection_sock   inet_conn
u16                           tcp_header_len          read_mostly         read_mostly         tcp_bound_to_half_wnd,tcp_current_mss(tx);tcp_rcv_established(rx)
u16                           gso_segs                read_mostly                             tcp_xmit_size_goal
__be32                        pred_flags              read_write          read_mostly         tcp_select_window(tx);tcp_rcv_established(rx)
u64                           bytes_received                              read_write          tcp_rcv_nxt_update(rx)
u32                           segs_in                                     read_write          tcp_v6_rcv(rx)
u32                           data_segs_in                                read_write          tcp_v6_rcv(rx)
u32                           rcv_nxt                 read_mostly         read_write          tcp_cleanup_rbuf,tcp_send_ack,tcp_inq_hint,tcp_transmit_skb,tcp_receive_window(tx);tcp_v6_do_rcv,tcp_rcv_established,tcp_data_queue,tcp_receive_window,tcp_rcv_nxt_update(write)(rx)
u32                           copied_seq                                  read_mostly         tcp_cleanup_rbuf,tcp_rcv_space_adjust,tcp_inq_hint
u32                           rcv_wup                                     read_write          __tcp_cleanup_rbuf,tcp_receive_window,tcp_receive_established
u32                           snd_nxt                 read_write          read_mostly         tcp_rate_check_app_limited,__tcp_transmit_skb,tcp_event_new_data_sent(write)(tx);tcp_rcv_established,tcp_ack,tcp_clean_rtx_queue(rx)
u32                           segs_out                read_write                              __tcp_transmit_skb
u32                           data_segs_out           read_write                              __tcp_transmit_skb,tcp_update_skb_after_send
u64                           bytes_sent              read_write                              __tcp_transmit_skb
u64                           bytes_acked                                 read_write          tcp_snd_una_update/tcp_ack
u32                           dsack_dups
u32                           snd_una                 read_mostly         read_write          tcp_wnd_end,tcp_urg_mode,tcp_minshall_check,tcp_cwnd_validate(tx);tcp_ack,tcp_may_update_window,tcp_clean_rtx_queue(write),tcp_ack_tstamp(rx)
u32                           snd_sml                 read_write                              tcp_minshall_check,tcp_minshall_update
u32                           rcv_tstamp                                  read_mostly         tcp_ack
u32                           lsndtime                read_write                              tcp_slow_start_after_idle_check,tcp_event_data_sent
u32                           last_oow_ack_time
u32                           compressed_ack_rcv_nxt
u32                           tsoffset                read_mostly         read_mostly         tcp_established_options(tx);tcp_fast_parse_options(rx)
struct list_head              tsq_node
struct list_head              tsorted_sent_queue      read_write                              tcp_update_skb_after_send
u32                           snd_wl1                                     read_mostly         tcp_may_update_window
u32                           snd_wnd                 read_mostly         read_mostly         tcp_wnd_end,tcp_tso_should_defer(tx);tcp_fast_path_on(rx)
u32                           max_window              read_mostly                             tcp_bound_to_half_wnd,forced_push
u32                           mss_cache               read_mostly         read_mostly         tcp_rate_check_app_limited,tcp_current_mss,tcp_sync_mss,tcp_sndbuf_expand,tcp_tso_should_defer(tx);tcp_update_pacing_rate,tcp_clean_rtx_queue(rx)
u32                           window_clamp            read_mostly         read_write          tcp_rcv_space_adjust,__tcp_select_window
u32                           rcv_ssthresh            read_mostly                             __tcp_select_window
u8                            scaling_ratio           read_mostly         read_mostly         tcp_win_from_space
struct                        tcp_rack
u16                           advmss                                      read_mostly         tcp_rcv_space_adjust
u8                            compressed_ack
u8:2                          dup_ack_counter
u8:1                          tlp_retrans
u8:1                          tcp_usec_ts             read_mostly         read_mostly
u32                           chrono_start            read_write                              tcp_chrono_start/stop(tcp_write_xmit,tcp_cwnd_validate,tcp_send_syn_data)
u32[3]                        chrono_stat             read_write                              tcp_chrono_start/stop(tcp_write_xmit,tcp_cwnd_validate,tcp_send_syn_data)
u8:2                          chrono_type             read_write                              tcp_chrono_start/stop(tcp_write_xmit,tcp_cwnd_validate,tcp_send_syn_data)
u8:1                          rate_app_limited                            read_write          tcp_rate_gen
u8:1                          fastopen_connect
u8:1                          fastopen_no_cookie
u8:1                          is_sack_reneg                               read_mostly         tcp_skb_entail,tcp_ack
u8:2                          fastopen_client_fail
u8:4                          nonagle                 read_write                              tcp_skb_entail,tcp_push_pending_frames
u8:1                          thin_lto
u8:1                          recvmsg_inq
u8:1                          repair                  read_mostly                             tcp_write_xmit
u8:1                          frto
u8                            repair_queue
u8:2                          save_syn
u8:1                          syn_data
u8:1                          syn_fastopen
u8:1                          syn_fastopen_exp
u8:1                          syn_fastopen_ch
u8:1                          syn_data_acked
u8:1                          is_cwnd_limited         read_mostly                             tcp_cwnd_validate,tcp_is_cwnd_limited
u32                           tlp_high_seq                                read_mostly         tcp_ack
u32                           tcp_tx_delay
u64                           tcp_wstamp_ns           read_write                              tcp_pacing_check,tcp_tso_should_defer,tcp_update_skb_after_send
u64                           tcp_clock_cache         read_write          read_write          tcp_mstamp_refresh(tcp_write_xmit/tcp_rcv_space_adjust),__tcp_transmit_skb,tcp_tso_should_defer;timer
u64                           tcp_mstamp              read_write          read_write          tcp_mstamp_refresh(tcp_write_xmit/tcp_rcv_space_adjust)(tx);tcp_rcv_space_adjust,tcp_rate_gen,tcp_clean_rtx_queue,tcp_ack_update_rtt/tcp_time_stamp(rx);timer
u32                           srtt_us                 read_mostly         read_write          tcp_tso_should_defer(tx);tcp_update_pacing_rate,__tcp_set_rto,tcp_rtt_estimator(rx)
u32                           mdev_us                 read_write                              tcp_rtt_estimator
u32                           mdev_max_us
u32                           rttvar_us                                   read_mostly         __tcp_set_rto
u32                           rtt_seq                 read_write                              tcp_rtt_estimator
struct minmax                 rtt_min                                     read_mostly         tcp_min_rtt/tcp_rate_gen,tcp_min_rtttcp_update_rtt_min
u32                           packets_out             read_write          read_write          tcp_packets_in_flight(tx/rx);tcp_slow_start_after_idle_check,tcp_nagle_check,tcp_rate_skb_sent,tcp_event_new_data_sent,tcp_cwnd_validate,tcp_write_xmit(tx);tcp_ack,tcp_clean_rtx_queue,tcp_update_pacing_rate(rx)
u32                           retrans_out                                 read_mostly         tcp_packets_in_flight,tcp_rate_check_app_limited
u32                           max_packets_out                             read_write          tcp_cwnd_validate
u32                           cwnd_usage_seq                              read_write          tcp_cwnd_validate
u16                           urg_data                                    read_mostly         tcp_fast_path_check
u8                            ecn_flags               read_write                              tcp_ecn_send
u8                            keepalive_probes
u32                           reordering              read_mostly                             tcp_sndbuf_expand
u32                           reord_seen
u32                           snd_up                  read_write          read_mostly         tcp_mark_urg,tcp_urg_mode,__tcp_transmit_skb(tx);tcp_clean_rtx_queue(rx)
struct tcp_options_received   rx_opt                  read_mostly         read_write          tcp_established_options(tx);tcp_fast_path_on,tcp_ack_update_window,tcp_is_sack,tcp_data_queue,tcp_rcv_established,tcp_ack_update_rtt(rx)
u32                           snd_ssthresh                                read_mostly         tcp_update_pacing_rate
u32                           snd_cwnd                read_mostly         read_mostly         tcp_snd_cwnd,tcp_rate_check_app_limited,tcp_tso_should_defer(tx);tcp_update_pacing_rate
u32                           snd_cwnd_cnt
u32                           snd_cwnd_clamp
u32                           snd_cwnd_used
u32                           snd_cwnd_stamp
u32                           prior_cwnd
u32                           prr_delivered
u32                           prr_out                 read_mostly         read_mostly         tcp_rate_skb_sent,tcp_newly_delivered(tx);tcp_ack,tcp_rate_gen,tcp_clean_rtx_queue(rx)
u32                           delivered               read_mostly         read_write          tcp_rate_skb_sent, tcp_newly_delivered(tx);tcp_ack, tcp_rate_gen, tcp_clean_rtx_queue (rx)
u32                           delivered_ce            read_mostly         read_write          tcp_rate_skb_sent(tx);tcp_rate_gen(rx)
u32                           lost                                        read_mostly         tcp_ack
u32                           app_limited             read_write          read_mostly         tcp_rate_check_app_limited,tcp_rate_skb_sent(tx);tcp_rate_gen(rx)
u64                           first_tx_mstamp         read_write                              tcp_rate_skb_sent
u64                           delivered_mstamp        read_write                              tcp_rate_skb_sent
u32                           rate_delivered                              read_mostly         tcp_rate_gen
u32                           rate_interval_us                            read_mostly         rate_delivered,rate_app_limited
u32                           rcv_wnd                 read_write          read_mostly         tcp_select_window,tcp_receive_window,tcp_fast_path_check
u32                           write_seq               read_write                              tcp_rate_check_app_limited,tcp_write_queue_empty,tcp_skb_entail,forced_push,tcp_mark_push
u32                           notsent_lowat           read_mostly                             tcp_stream_memory_free
u32                           pushed_seq              read_write                              tcp_mark_push,forced_push
u32                           lost_out                read_mostly         read_mostly         tcp_left_out(tx);tcp_packets_in_flight(tx/rx);tcp_rate_check_app_limited(rx)
u32                           sacked_out              read_mostly         read_mostly         tcp_left_out(tx);tcp_packets_in_flight(tx/rx);tcp_clean_rtx_queue(rx)
struct hrtimer                pacing_timer
struct hrtimer                compressed_ack_timer
struct sk_buff*               lost_skb_hint           read_mostly                             tcp_clean_rtx_queue
struct sk_buff*               retransmit_skb_hint     read_mostly                             tcp_clean_rtx_queue
struct rb_root                out_of_order_queue                          read_mostly         tcp_data_queue,tcp_fast_path_check
struct sk_buff*               ooo_last_skb
struct tcp_sack_block[1]      duplicate_sack
struct tcp_sack_block[4]      selective_acks
struct tcp_sack_block[4]      recv_sack_cache
struct sk_buff*               highest_sack            read_write                              tcp_event_new_data_sent
int                           lost_cnt_hint
u32                           prior_ssthresh
u32                           high_seq
u32                           retrans_stamp
u32                           undo_marker
int                           undo_retrans
u64                           bytes_retrans
u32                           total_retrans
u32                           rto_stamp
u16                           total_rto
u16                           total_rto_recoveries
u32                           total_rto_time
u32                           urg_seq
unsigned_int                  keepalive_time
unsigned_int                  keepalive_intvl
int                           linger2
u8                            bpf_sock_ops_cb_flags
u8:1                          bpf_chg_cc_inprogress
u16                           timeout_rehash
u32                           rcv_ooopack
u32                           rcv_rtt_last_tsecr
struct                        rcv_rtt_est                                 read_write          tcp_rcv_space_adjust,tcp_rcv_established
struct                        rcvq_space                                  read_write          tcp_rcv_space_adjust
struct                        mtu_probe
u32                           plb_rehash
u32                           mtu_info
bool                          is_mptcp
bool                          smc_hs_congested
bool                          syn_smc
struct tcp_sock_af_ops*       af_specific
struct tcp_md5sig_info*       md5sig_info
struct tcp_fastopen_request*  fastopen_req
struct request_sock*          fastopen_rsk
struct saved_syn*             saved_syn
============================= ======================= =================== =================== ==================================================================================================================================================================================================================
