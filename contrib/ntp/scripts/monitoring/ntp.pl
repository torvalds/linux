#!/usr/bin/perl -w
;#
;# ntp.pl,v 3.1 1993/07/06 01:09:09 jbj Exp
;#
;# process loop filter statistics file and either
;#     - show statistics periodically using gnuplot
;#     - or print a single plot
;#
;#  Copyright (c) 1992 
;#  Rainer Pruy Friedrich-Alexander Universitaet Erlangen-Nuernberg
;#
;#
;#############################################################

package ntp;

$NTP_version = 2;
$ctrl_mode=6;

$byte1 = (($NTP_version & 0x7)<< 3) & 0x34 | ($ctrl_mode & 0x7);
$MAX_DATA = 468;

$sequence = 0;			# initial sequence number incred before used
$pad=4;
$do_auth=0;			# no possibility today
$keyid=0;
;#list if known keys (passwords)
%KEYS = ( 0, "\200\200\200\200\200\200\200\200",
	 );

;#-----------------------------------------------------------------------------
;# access routines for ntp control packet
    ;# NTP control message format
    ;#  C  LI|VN|MODE  LI 2bit=00  VN 3bit=2(3) MODE 3bit=6 : $byte1
    ;#  C  R|E|M|Op    R response  E error    M more   Op opcode
    ;#  n  sequence
    ;#  n  status
    ;#  n  associd
    ;#  n  offset
    ;#  n  count
    ;#  a+ data (+ padding)
    ;#  optional authentication data
    ;#  N  key
    ;#  N2 checksum
    
;# first byte of packet
sub pkt_LI   { return ($_[$[] >> 6) & 0x3; }
sub pkt_VN   { return ($_[$[] >> 3) & 0x7; }
sub pkt_MODE { return ($_[$[]     ) & 0x7; }

;# second byte of packet
sub pkt_R  { return ($_[$[] & 0x80) == 0x80; }
sub pkt_E  { return ($_[$[] & 0x40) == 0x40; }
sub pkt_M  { return ($_[$[] & 0x20) == 0x20; }
sub pkt_OP { return $_[$[] & 0x1f; }

;#-----------------------------------------------------------------------------

sub setkey
{
    local($id,$key) = @_;

    $KEYS{$id} = $key if (defined($key));
    if (! defined($KEYS{$id}))
    {
	warn "Key $id not yet specified - key not changed\n";
	return undef;
    }
    return ($keyid,$keyid = $id)[$[];
}

;#-----------------------------------------------------------------------------
sub numerical { $a <=> $b; }

;#-----------------------------------------------------------------------------

sub send	#'
{
    local($fh,$opcode, $associd, $data,$address) = @_;
    $fh = caller(0)."'$fh";

    local($junksize,$junk,$packet,$offset,$ret);
    $offset = 0;

    $sequence++;
    while(1)
    {
	$junksize = length($data);
	$junksize = $MAX_DATA if $junksize > $MAX_DATA;
	
	($junk,$data) = $data =~ /^(.{$junksize})(.*)$/;
	$packet
	    = pack("C2n5a".(($junk eq "") ? 0 : &pad($junksize+12,$pad)-12),
		   $byte1,
		   ($opcode & 0x1f) | ($data ? 0x20 : 0),
		   $sequence,
		   0, $associd,
		   $offset, $junksize, $junk);
	if ($do_auth)
	{
	    ;# not yet
	}
	$offset += $junksize;

	if (defined($address))
	{
	    $ret = send($fh, $packet, 0, $address);
	}
	else
	{
	    $ret = send($fh, $packet, 0);
	}

	if (! defined($ret))
	{
	    warn "send failed: $!\n";
	    return undef;
	}
	elsif ($ret != length($packet))
	{
	    warn "send failed: sent only $ret from ".length($packet). "bytes\n";
	    return undef;
	}
	return $sequence unless $data;
    }
}

;#-----------------------------------------------------------------------------
;# status interpretation
;#
sub getval
{
    local($val,*list) = @_;
    
    return $list{$val} if defined($list{$val});
    return sprintf("%s#%d",$list{"-"},$val) if defined($list{"-"});
    return "unknown-$val";
}

;#---------------------------------
;# system status
;#
;# format: |LI|CS|SECnt|SECode| LI=2bit CS=6bit SECnt=4bit SECode=4bit
sub ssw_LI     { return ($_[$[] >> 14) & 0x3; }
sub ssw_CS     { return ($_[$[] >> 8)  & 0x3f; }
sub ssw_SECnt  { return ($_[$[] >> 4)  & 0xf; }
sub ssw_SECode { return $_[$[] & 0xf; }

%LI = ( 0, "leap_none",  1, "leap_add_sec", 2, "leap_del_sec", 3, "sync_alarm", "-", "leap");
%ClockSource = (0, "sync_unspec",
		1, "sync_pps",
		2, "sync_lf_clock",
		3, "sync_hf_clock",
		4, "sync_uhf_clock",
		5, "sync_local_proto",
		6, "sync_ntp",
		7, "sync_udp/time",
		8, "sync_wristwatch",
		9, "sync_telephone",
		"-", "ClockSource",
		);

%SystemEvent = (0, "event_unspec",
		1, "event_freq_not_set",
		2, "event_freq_set",
		3, "event_spike_detect",
		4, "event_freq_mode",
		5, "event_clock_sync",
		6, "event_restart",
		7, "event_panic_stop",
		8, "event_no_sys_peer",
		9, "event_leap_armed",
		10, "event_leap_disarmed",
		11, "event_leap_event",
		12, "event_clock_step",
		13, "event_kern",
		14, "event_loaded_leaps",
		15, "event_stale_leaps",
		"-", "event",
		);
sub LI
{
    &getval(&ssw_LI($_[$[]),*LI);
}
sub ClockSource
{
    &getval(&ssw_CS($_[$[]),*ClockSource);
}

sub SystemEvent
{
    &getval(&ssw_SECode($_[$[]),*SystemEvent);
}

sub system_status
{
    return sprintf("%s, %s, %d event%s, %s", &LI($_[$[]), &ClockSource($_[$[]),
		   &ssw_SECnt($_[$[]), ((&ssw_SECnt($_[$[])==1) ? "" : "s"),
		   &SystemEvent($_[$[]));
}
;#---------------------------------
;# peer status
;#
;# format: |PStat|PSel|PCnt|PCode| Pstat=6bit PSel=2bit PCnt=4bit PCode=4bit
sub psw_PStat_config     { return ($_[$[] & 0x8000) == 0x8000; }
sub psw_PStat_authenable { return ($_[$[] & 0x4000) == 0x4000; }
sub psw_PStat_authentic  { return ($_[$[] & 0x2000) == 0x2000; }
sub psw_PStat_reach      { return ($_[$[] & 0x1000) == 0x1000; }
sub psw_PStat_bcast      { return ($_[$[] & 0x0800) == 0x0800; }
sub psw_PStat { return ($_[$[] >> 10) & 0x3f; }
sub psw_PSel  { return ($_[$[] >> 8)  & 0x3;  }
sub psw_PCnt  { return ($_[$[] >> 4)  & 0xf; }
sub psw_PCode { return $_[$[] & 0xf; }

%PeerSelection = (0, "sel_reject",
		  1, "sel_falsetick",
		  2, "sel_excess",
		  3, "sel_outlier",
		  4, "sel_candidate",
		  5, "sel_backup",
		  6, "sel_sys.peer",
		  6, "sel_pps.peer",
		  "-", "PeerSel",
		  );
%PeerEvent = (0, "event_unspec",
	      1, "event_mobilize",
	      2, "event_demobilize",
	      3, "event_unreach",
	      4, "event_reach",
	      5, "event_restart",
	      6, "event_no_reply",
	      7, "event_rate_exceed",
	      8, "event_denied",
	      9, "event_leap_armed",
	      10, "event_sys_peer",
	      11, "event_clock_event",
	      12, "event_bad_auth",
	      13, "event_popcorn",
	      14, "event_intlv_mode",
	      15, "event_intlv_err",
	      "-", "event",
	      );

sub PeerSelection
{
    &getval(&psw_PSel($_[$[]),*PeerSelection);
}

sub PeerEvent
{
    &getval(&psw_PCode($_[$[]),*PeerEvent);
}

sub peer_status
{
    local($x) = ("");
    $x .= "config,"     if &psw_PStat_config($_[$[]);
    $x .= "authenable," if &psw_PStat_authenable($_[$[]);
    $x .= "authentic,"  if &psw_PStat_authentic($_[$[]);
    $x .= "reach,"      if &psw_PStat_reach($_[$[]);
    $x .= "bcast,"      if &psw_PStat_bcast($_[$[]);

    $x .= sprintf(" %s, %d event%s, %s", &PeerSelection($_[$[]),
		  &psw_PCnt($_[$[]), ((&psw_PCnt($_[$[]) == 1) ? "" : "s"),
		  &PeerEvent($_[$[]));
    return $x;
}

;#---------------------------------
;# clock status
;#
;# format: |CStat|CEvnt| CStat=8bit CEvnt=8bit
sub csw_CStat { return ($_[$[] >> 8) & 0xff; }
sub csw_CEvnt { return $_[$[] & 0xff; }

%ClockStatus = (0, "clk_nominal",
		1, "clk_timeout",
		2, "clk_badreply",
		3, "clk_fault",
		4, "clk_badsig",
		5, "clk_baddate",
		6, "clk_badtime",
		"-", "clk",
	       );

sub clock_status
{
    return sprintf("%s, last %s",
		   &getval(&csw_CStat($_[$[]),*ClockStatus),
		   &getval(&csw_CEvnt($_[$[]),*ClockStatus));
}

;#---------------------------------
;# error status
;#
;# format: |Err|reserved|  Err=8bit
;#
sub esw_Err { return ($_[$[] >> 8) & 0xff; }

%ErrorStatus = (0, "err_unspec",
		1, "err_auth_fail",
		2, "err_invalid_fmt",
		3, "err_invalid_opcode",
		4, "err_unknown_assoc",
		5, "err_unknown_var",
		6, "err_invalid_value",
		7, "err_adm_prohibit",
		);

sub error_status
{
    return sprintf("%s", &getval(&esw_Err($_[$[]),*ErrorStatus));
}

;#-----------------------------------------------------------------------------
;#
;# cntrl op name translation

%CntrlOpName = (0, "reserved",
		1, "read_status",
		2, "read_variables",
		3, "write_variables",
		4, "read_clock_variables",
		5, "write_clock_variables",
		6, "set_trap",
		7, "trap_response",
		8, "configure",
		9, "saveconf",
		10, "read_mru",
		11, "read_ordlist",
		12, "rqst_nonce",
		31, "unset_trap", # !!! unofficial !!!
		"-", "cntrlop",
		);

sub cntrlop_name
{
    return &getval($_[$[],*CntrlOpName);
}

;#-----------------------------------------------------------------------------

$STAT_short_pkt = 0;
$STAT_pkt = 0;

;# process a NTP control message (response) packet
;# returns a list ($ret,$data,$status,$associd,$op,$seq,$auth_keyid)
;#      $ret: undef     --> not yet complete
;#            ""        --> complete packet received
;#            "ERROR"   --> error during receive, bad packet, ...
;#          else        --> error packet - list may contain useful info


sub handle_packet
{
    local($pkt,$from) = @_;	# parameters
    local($len_pkt) = (length($pkt));
;#    local(*FRAGS,*lastseen);
    local($li_vn_mode,$r_e_m_op,$seq,$status,$associd,$offset,$count,$data);
    local($autch_keyid,$auth_cksum);

    $STAT_pkt++;
    if ($len_pkt < 12)
    {
	$STAT_short_pkt++;
	return ("ERROR","short packet received");
    }

    ;# now break packet apart
    ($li_vn_mode,$r_e_m_op,$seq,$status,$associd,$offset,$count,$data) =
	unpack("C2n5a".($len_pkt-12),$pkt);
    $data=substr($data,$[,$count);
    if ((($len_pkt - 12) - &pad($count,4)) >= 12)
    {
	;# looks like an authenticator
	($auth_keyid,$auth_cksum) =
	    unpack("Na8",substr($pkt,$len_pkt-12+$[,12));
	$STAT_auth++;
	;# no checking of auth_cksum (yet ?)
    }

    if (&pkt_VN($li_vn_mode) != $NTP_version)
    {
	$STAT_bad_version++;
	return ("ERROR","version ".&pkt_VN($li_vn_mode)."packet ignored");
    }

    if (&pkt_MODE($li_vn_mode) != $ctrl_mode)
    {
	$STAT_bad_mode++;
	return ("ERROR", "mode ".&pkt_MODE($li_vn_mode)." packet ignored");
    }
    
    ;# handle single fragment fast
    if ($offset == 0 && &pkt_M($r_e_m_op) == 0)
    {
	$STAT_single_frag++;
	if (&pkt_E($r_e_m_op))
	{
	    $STAT_err_pkt++;
	    return (&error_status($status),
		    $data,$status,$associd,&pkt_OP($r_e_m_op),$seq,
		    $auth_keyid);
	}
	else
	{
	    return ("",
		    $data,$status,$associd,&pkt_OP($r_e_m_op),$seq,
		    $auth_keyid);
	}
    }
    else
    {
	;# fragment - set up local name space
	$id = "$from$seq".&pkt_OP($r_e_m_op);
	$ID{$id} = 1;
	*FRAGS = "$id FRAGS";
	*lastseen = "$id lastseen";
	
	$STAT_frag++;
	
	$lastseen = 1 if !&pkt_M($r_e_m_op);
	if (!%FRAGS)
	{
	    print((&pkt_M($r_e_m_op) ? " more" : "")."\n");
	    $FRAGS{$offset} = $data;
	    ;# save other info
	    @FRAGS = ($status,$associd,&pkt_OP($r_e_m_op),$seq,$auth_keyid,$r_e_m_op);
	}
	else
	{
	    print((&pkt_M($r_e_m_op) ? " more" : "")."\n");
	    ;# add frag to previous - combine on the fly
	    if (defined($FRAGS{$offset}))
	    {
		$STAT_dup_frag++;
		return ("ERROR","duplicate fragment at $offset seq=$seq");
	    }
	    
	    $FRAGS{$offset} = $data;
	    
	    undef($loff);
	    foreach $off (sort numerical keys(%FRAGS))
	    {
		next unless defined($FRAGS{$off});
		if (defined($loff) &&
		    ($loff + length($FRAGS{$loff})) == $off)
		{
		    $FRAGS{$loff} .= $FRAGS{$off};
		    delete $FRAGS{$off};
		    last;
		}
		$loff = $off;
	    }

	    ;# return packet if all frags arrived
	    ;# at most two frags with possible padding ???
	    if ($lastseen && defined($FRAGS{0}) &&
		(((scalar(@x=sort numerical keys(%FRAGS)) == 2) &&
		  (length($FRAGS{0}) + 8) > $x[$[+1]) ||
		  (scalar(@x=sort numerical keys(%FRAGS)) < 2)))
	    {
		@x=((&pkt_E($r_e_m_op) ? &error_status($status) : ""),
		    $FRAGS{0},@FRAGS);
		&pkt_E($r_e_m_op) ? $STAT_err_frag++ : $STAT_frag_all++;
		undef(%FRAGS);
		undef(@FRAGS);
		undef($lastseen);
		delete $ID{$id};
		&main'clear_timeout($id);
		return @x;
	    }
	    else
	    {
		&main'set_timeout($id,time+$timeout,"&ntp'handle_packet_timeout(\"".unpack("H*",$id)."\");"); #'";
	    }
	}
	return (undef);
    }
}

sub handle_packet_timeout
{
    local($id) = @_;
    local($r_e_m_op,*FRAGS,*lastseen,@x) = (@FRAGS[$[+5]);
    
    *FRAGS = "$id FRAGS";
    *lastseen = "$id lastseen";
    
    @x=((&pkt_E($r_e_m_op) ? &error_status($status) : "TIMEOUT"),
	$FRAGS{0},@FRAGS[$[ .. $[+4]);
    $STAT_frag_timeout++;
    undef(%FRAGS);
    undef(@FRAGS);
    undef($lastseen);
    delete $ID{$id};
    return @x;
}


sub pad
{
    return $_[$[+1] * int(($_[$[] + $_[$[+1] - 1) / $_[$[+1]);
}

1;
