.\"	$OpenBSD: nchan.ms,v 1.8 2003/11/21 11:57:03 djm Exp $
.\"
.\"
.\" Copyright (c) 1999 Markus Friedl.  All rights reserved.
.\"
.\" Redistribution and use in source and binary forms, with or without
.\" modification, are permitted provided that the following conditions
.\" are met:
.\" 1. Redistributions of source code must retain the above copyright
.\"    notice, this list of conditions and the following disclaimer.
.\" 2. Redistributions in binary form must reproduce the above copyright
.\"    notice, this list of conditions and the following disclaimer in the
.\"    documentation and/or other materials provided with the distribution.
.\"
.\" THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
.\" IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
.\" OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
.\" IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
.\" INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
.\" NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
.\" DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
.\" THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
.\" (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
.\" THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
.\"
.TL
OpenSSH Channel Close Protocol 1.5 Implementation
.SH
Channel Input State Diagram
.PS
reset
l=1
s=1.2
ellipsewid=s*ellipsewid
boxwid=s*boxwid
ellipseht=s*ellipseht
S1: ellipse "INPUT" "OPEN"
move right 2*l from last ellipse.e
S4: ellipse "INPUT" "CLOSED"
move down l from last ellipse.s
S3: ellipse "INPUT" "WAIT" "OCLOSED"
move down l from 1st ellipse.s
S2: ellipse "INPUT" "WAIT" "DRAIN"
arrow "" "rcvd OCLOSE/" "shutdown_read" "send IEOF" from S1.e to S4.w
arrow "ibuf_empty/" "send IEOF" from S2.e to S3.w
arrow from S1.s to S2.n
box invis "read_failed/" "shutdown_read" with .e at last arrow.c
arrow  from S3.n to S4.s
box invis "rcvd OCLOSE/" "-" with .w at last arrow.c
ellipse wid .9*ellipsewid ht .9*ellipseht at S4
arrow "start" "" from S1.w+(-0.5,0) to S1.w
arrow from S2.ne to S4.sw
box invis "rcvd OCLOSE/     " with .e at last arrow.c
box invis " send IEOF" with .w at last arrow.c
.PE
.SH
Channel Output State Diagram
.PS
S1: ellipse "OUTPUT" "OPEN"
move right 2*l from last ellipse.e
S3: ellipse "OUTPUT" "WAIT" "IEOF"
move down l from last ellipse.s
S4: ellipse "OUTPUT" "CLOSED"
move down l from 1st ellipse.s
S2: ellipse "OUTPUT" "WAIT" "DRAIN"
arrow "" "write_failed/" "shutdown_write" "send OCLOSE" from S1.e to S3.w
arrow "obuf_empty ||" "write_failed/" "shutdown_write" "send OCLOSE" from S2.e to S4.w
arrow from S1.s to S2.n
box invis "rcvd IEOF/" "-" with .e at last arrow.c
arrow from S3.s to S4.n
box invis "rcvd IEOF/" "-" with .w at last arrow.c
ellipse wid .9*ellipsewid ht .9*ellipseht at S4
arrow "start" "" from S1.w+(-0.5,0) to S1.w
.PE
.SH
Notes
.PP
The input buffer is filled with data from the socket
(the socket represents the local consumer/producer of the
forwarded channel).
The data is then sent over the INPUT-end (transmit-end) of the channel to the
remote peer.
Data sent by the peer is received on the OUTPUT-end (receive-end),
saved in the output buffer and written to the socket.
.PP
If the local protocol instance has forwarded all data on the
INPUT-end of the channel, it sends an IEOF message to the peer.
If the peer receives the IEOF and has consumed all
data he replies with an OCLOSE.
When the local instance receives the OCLOSE
he considers the INPUT-half of the channel closed.
The peer has his OUTOUT-half closed.
.PP
A channel can be deallocated by a protocol instance
if both the INPUT- and the OUTOUT-half on his
side of the channel are closed.
Note that when an instance is unable to consume the
received data, he is permitted to send an OCLOSE
before the matching IEOF is received.
