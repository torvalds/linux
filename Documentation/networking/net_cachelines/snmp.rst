.. SPDX-License-Identifier: GPL-2.0
.. Copyright (C) 2023 Google LLC

===========================================
netns_ipv4 enum fast path usage breakdown
===========================================

============== ===================================== =================== =================== ==================================================
Type           Name                                  fastpath_tx_access  fastpath_rx_access  comment
============== ===================================== =================== =================== ==================================================
unsigned_long  LINUX_MIB_TCPKEEPALIVE                write_mostly                            tcp_keepalive_timer
unsigned_long  LINUX_MIB_DELAYEDACKS                 write_mostly                            tcp_delack_timer_handler,tcp_delack_timer
unsigned_long  LINUX_MIB_DELAYEDACKLOCKED            write_mostly                            tcp_delack_timer_handler,tcp_delack_timer
unsigned_long  LINUX_MIB_TCPAUTOCORKING              write_mostly                            tcp_push,tcp_sendmsg_locked
unsigned_long  LINUX_MIB_TCPFROMZEROWINDOWADV        write_mostly                            tcp_select_window,tcp_transmit-skb
unsigned_long  LINUX_MIB_TCPTOZEROWINDOWADV          write_mostly                            tcp_select_window,tcp_transmit-skb
unsigned_long  LINUX_MIB_TCPWANTZEROWINDOWADV        write_mostly                            tcp_select_window,tcp_transmit-skb
unsigned_long  LINUX_MIB_TCPORIGDATASENT             write_mostly                            tcp_write_xmit
unsigned_long  LINUX_MIB_TCPHPHITS                                       write_mostly        tcp_rcv_established,tcp_v4_do_rcv,tcp_v6_do_rcv
unsigned_long  LINUX_MIB_TCPRCVCOALESCE                                  write_mostly        tcp_try_coalesce,tcp_queue_rcv,tcp_rcv_established
unsigned_long  LINUX_MIB_TCPPUREACKS                                     write_mostly        tcp_ack,tcp_rcv_established
unsigned_long  LINUX_MIB_TCPHPACKS                                       write_mostly        tcp_ack,tcp_rcv_established
unsigned_long  LINUX_MIB_TCPDELIVERED                                    write_mostly        tcp_newly_delivered,tcp_ack,tcp_rcv_established
unsigned_long  LINUX_MIB_SYNCOOKIESSENT
unsigned_long  LINUX_MIB_SYNCOOKIESRECV
unsigned_long  LINUX_MIB_SYNCOOKIESFAILED
unsigned_long  LINUX_MIB_EMBRYONICRSTS
unsigned_long  LINUX_MIB_PRUNECALLED
unsigned_long  LINUX_MIB_RCVPRUNED
unsigned_long  LINUX_MIB_OFOPRUNED
unsigned_long  LINUX_MIB_OUTOFWINDOWICMPS
unsigned_long  LINUX_MIB_LOCKDROPPEDICMPS
unsigned_long  LINUX_MIB_ARPFILTER
unsigned_long  LINUX_MIB_TIMEWAITED
unsigned_long  LINUX_MIB_TIMEWAITRECYCLED
unsigned_long  LINUX_MIB_TIMEWAITKILLED
unsigned_long  LINUX_MIB_PAWSACTIVEREJECTED
unsigned_long  LINUX_MIB_PAWSESTABREJECTED
unsigned_long  LINUX_MIB_BEYOND_WINDOW
unsigned_long  LINUX_MIB_TSECR_REJECTED
unsigned_long  LINUX_MIB_PAWS_OLD_ACK
unsigned_long  LINUX_MIB_PAWS_TW_REJECTED
unsigned_long  LINUX_MIB_DELAYEDACKLOST
unsigned_long  LINUX_MIB_LISTENOVERFLOWS
unsigned_long  LINUX_MIB_LISTENDROPS
unsigned_long  LINUX_MIB_TCPRENORECOVERY
unsigned_long  LINUX_MIB_TCPSACKRECOVERY
unsigned_long  LINUX_MIB_TCPSACKRENEGING
unsigned_long  LINUX_MIB_TCPSACKREORDER
unsigned_long  LINUX_MIB_TCPRENOREORDER
unsigned_long  LINUX_MIB_TCPTSREORDER
unsigned_long  LINUX_MIB_TCPFULLUNDO
unsigned_long  LINUX_MIB_TCPPARTIALUNDO
unsigned_long  LINUX_MIB_TCPDSACKUNDO
unsigned_long  LINUX_MIB_TCPLOSSUNDO
unsigned_long  LINUX_MIB_TCPLOSTRETRANSMIT
unsigned_long  LINUX_MIB_TCPRENOFAILURES
unsigned_long  LINUX_MIB_TCPSACKFAILURES
unsigned_long  LINUX_MIB_TCPLOSSFAILURES
unsigned_long  LINUX_MIB_TCPFASTRETRANS
unsigned_long  LINUX_MIB_TCPSLOWSTARTRETRANS
unsigned_long  LINUX_MIB_TCPTIMEOUTS
unsigned_long  LINUX_MIB_TCPLOSSPROBES
unsigned_long  LINUX_MIB_TCPLOSSPROBERECOVERY
unsigned_long  LINUX_MIB_TCPRENORECOVERYFAIL
unsigned_long  LINUX_MIB_TCPSACKRECOVERYFAIL
unsigned_long  LINUX_MIB_TCPRCVCOLLAPSED
unsigned_long  LINUX_MIB_TCPDSACKOLDSENT
unsigned_long  LINUX_MIB_TCPDSACKOFOSENT
unsigned_long  LINUX_MIB_TCPDSACKRECV
unsigned_long  LINUX_MIB_TCPDSACKOFORECV
unsigned_long  LINUX_MIB_TCPABORTONDATA
unsigned_long  LINUX_MIB_TCPABORTONCLOSE
unsigned_long  LINUX_MIB_TCPABORTONMEMORY
unsigned_long  LINUX_MIB_TCPABORTONTIMEOUT
unsigned_long  LINUX_MIB_TCPABORTONLINGER
unsigned_long  LINUX_MIB_TCPABORTFAILED
unsigned_long  LINUX_MIB_TCPMEMORYPRESSURES
unsigned_long  LINUX_MIB_TCPMEMORYPRESSURESCHRONO
unsigned_long  LINUX_MIB_TCPSACKDISCARD
unsigned_long  LINUX_MIB_TCPDSACKIGNOREDOLD
unsigned_long  LINUX_MIB_TCPDSACKIGNOREDNOUNDO
unsigned_long  LINUX_MIB_TCPSPURIOUSRTOS
unsigned_long  LINUX_MIB_TCPMD5NOTFOUND
unsigned_long  LINUX_MIB_TCPMD5UNEXPECTED
unsigned_long  LINUX_MIB_TCPMD5FAILURE
unsigned_long  LINUX_MIB_SACKSHIFTED
unsigned_long  LINUX_MIB_SACKMERGED
unsigned_long  LINUX_MIB_SACKSHIFTFALLBACK
unsigned_long  LINUX_MIB_TCPBACKLOGDROP
unsigned_long  LINUX_MIB_PFMEMALLOCDROP
unsigned_long  LINUX_MIB_TCPMINTTLDROP
unsigned_long  LINUX_MIB_TCPDEFERACCEPTDROP
unsigned_long  LINUX_MIB_IPRPFILTER
unsigned_long  LINUX_MIB_TCPTIMEWAITOVERFLOW
unsigned_long  LINUX_MIB_TCPREQQFULLDOCOOKIES
unsigned_long  LINUX_MIB_TCPREQQFULLDROP
unsigned_long  LINUX_MIB_TCPRETRANSFAIL
unsigned_long  LINUX_MIB_TCPBACKLOGCOALESCE
unsigned_long  LINUX_MIB_TCPOFOQUEUE
unsigned_long  LINUX_MIB_TCPOFODROP
unsigned_long  LINUX_MIB_TCPOFOMERGE
unsigned_long  LINUX_MIB_TCPCHALLENGEACK
unsigned_long  LINUX_MIB_TCPSYNCHALLENGE
unsigned_long  LINUX_MIB_TCPFASTOPENACTIVE
unsigned_long  LINUX_MIB_TCPFASTOPENACTIVEFAIL
unsigned_long  LINUX_MIB_TCPFASTOPENPASSIVE
unsigned_long  LINUX_MIB_TCPFASTOPENPASSIVEFAIL
unsigned_long  LINUX_MIB_TCPFASTOPENLISTENOVERFLOW
unsigned_long  LINUX_MIB_TCPFASTOPENCOOKIEREQD
unsigned_long  LINUX_MIB_TCPFASTOPENBLACKHOLE
unsigned_long  LINUX_MIB_TCPSPURIOUS_RTX_HOSTQUEUES
unsigned_long  LINUX_MIB_BUSYPOLLRXPACKETS
unsigned_long  LINUX_MIB_TCPSYNRETRANS
unsigned_long  LINUX_MIB_TCPHYSTARTTRAINDETECT
unsigned_long  LINUX_MIB_TCPHYSTARTTRAINCWND
unsigned_long  LINUX_MIB_TCPHYSTARTDELAYDETECT
unsigned_long  LINUX_MIB_TCPHYSTARTDELAYCWND
unsigned_long  LINUX_MIB_TCPACKSKIPPEDSYNRECV
unsigned_long  LINUX_MIB_TCPACKSKIPPEDPAWS
unsigned_long  LINUX_MIB_TCPACKSKIPPEDSEQ
unsigned_long  LINUX_MIB_TCPACKSKIPPEDFINWAIT2
unsigned_long  LINUX_MIB_TCPACKSKIPPEDTIMEWAIT
unsigned_long  LINUX_MIB_TCPACKSKIPPEDCHALLENGE
unsigned_long  LINUX_MIB_TCPWINPROBE
unsigned_long  LINUX_MIB_TCPMTUPFAIL
unsigned_long  LINUX_MIB_TCPMTUPSUCCESS
unsigned_long  LINUX_MIB_TCPDELIVEREDCE
unsigned_long  LINUX_MIB_TCPACKCOMPRESSED
unsigned_long  LINUX_MIB_TCPZEROWINDOWDROP
unsigned_long  LINUX_MIB_TCPRCVQDROP
unsigned_long  LINUX_MIB_TCPWQUEUETOOBIG
unsigned_long  LINUX_MIB_TCPFASTOPENPASSIVEALTKEY
unsigned_long  LINUX_MIB_TCPTIMEOUTREHASH
unsigned_long  LINUX_MIB_TCPDUPLICATEDATAREHASH
unsigned_long  LINUX_MIB_TCPDSACKRECVSEGS
unsigned_long  LINUX_MIB_TCPDSACKIGNOREDDUBIOUS
unsigned_long  LINUX_MIB_TCPMIGRATEREQSUCCESS
unsigned_long  LINUX_MIB_TCPMIGRATEREQFAILURE
unsigned_long  __LINUX_MIB_MAX
============== ===================================== =================== =================== ==================================================
