# tcpdump

[![Build
Status](https://travis-ci.org/the-tcpdump-group/tcpdump.png)](https://travis-ci.org/the-tcpdump-group/tcpdump)

To report a security issue please send an e-mail to security@tcpdump.org.

To report bugs and other problems, contribute patches, request a
feature, provide generic feedback etc please see the file
CONTRIBUTING in the tcpdump source tree root.

TCPDUMP 4.x.y
Now maintained by "The Tcpdump Group"
See 		www.tcpdump.org

Anonymous Git is available via:

	git clone git://bpf.tcpdump.org/tcpdump

formerly from 	Lawrence Berkeley National Laboratory
		Network Research Group <tcpdump@ee.lbl.gov>  
		ftp://ftp.ee.lbl.gov/old/tcpdump.tar.Z (3.4)

This directory contains source code for tcpdump, a tool for network
monitoring and data acquisition.  This software was originally
developed by the Network Research Group at the Lawrence Berkeley
National Laboratory.  The original distribution is available via
anonymous ftp to `ftp.ee.lbl.gov`, in `tcpdump.tar.Z`.  More recent
development is performed at tcpdump.org, http://www.tcpdump.org/

Tcpdump uses libpcap, a system-independent interface for user-level
packet capture.  Before building tcpdump, you must first retrieve and
build libpcap, also originally from LBL and now being maintained by
tcpdump.org; see http://www.tcpdump.org/ .

Once libpcap is built (either install it or make sure it's in
`../libpcap`), you can build tcpdump using the procedure in the `INSTALL.txt`
file.

The program is loosely based on SMI's "etherfind" although none of the
etherfind code remains.  It was originally written by Van Jacobson as
part of an ongoing research project to investigate and improve tcp and
internet gateway performance.  The parts of the program originally
taken from Sun's etherfind were later re-written by Steven McCanne of
LBL.  To insure that there would be no vestige of proprietary code in
tcpdump, Steve wrote these pieces from the specification given by the
manual entry, with no access to the source of tcpdump or etherfind.

Over the past few years, tcpdump has been steadily improved by the
excellent contributions from the Internet community (just browse
through the `CHANGES` file).  We are grateful for all the input.

Richard Stevens gives an excellent treatment of the Internet protocols
in his book *"TCP/IP Illustrated, Volume 1"*. If you want to learn more
about tcpdump and how to interpret its output, pick up this book.

Some tools for viewing and analyzing tcpdump trace files are available
from the Internet Traffic Archive:

* http://www.sigcomm.org/ITA/

Another tool that tcpdump users might find useful is tcpslice:

* https://github.com/the-tcpdump-group/tcpslice

It is a program that can be used to extract portions of tcpdump binary
trace files. See the above distribution for further details and
documentation.

Current versions can be found at www.tcpdump.org.

 - The TCPdump team

original text by: Steve McCanne, Craig Leres, Van Jacobson

-------------------------------------
```
This directory also contains some short awk programs intended as
examples of ways to reduce tcpdump data when you're tracking
particular network problems:

send-ack.awk
	Simplifies the tcpdump trace for an ftp (or other unidirectional
	tcp transfer).  Since we assume that one host only sends and
	the other only acks, all address information is left off and
	we just note if the packet is a "send" or an "ack".

	There is one output line per line of the original trace.
	Field 1 is the packet time in decimal seconds, relative
	to the start of the conversation.  Field 2 is delta-time
	from last packet.  Field 3 is packet type/direction.
	"Send" means data going from sender to receiver, "ack"
	means an ack going from the receiver to the sender.  A
	preceding "*" indicates that the data is a retransmission.
	A preceding "-" indicates a hole in the sequence space
	(i.e., missing packet(s)), a "#" means an odd-size (not max
	seg size) packet.  Field 4 has the packet flags
	(same format as raw trace).  Field 5 is the sequence
	number (start seq. num for sender, next expected seq number
	for acks).  The number in parens following an ack is
	the delta-time from the first send of the packet to the
	ack.  A number in parens following a send is the
	delta-time from the first send of the packet to the
	current send (on duplicate packets only).  Duplicate
	sends or acks have a number in square brackets showing
	the number of duplicates so far.

	Here is a short sample from near the start of an ftp:
		3.00    0.20   send . 512
		3.20    0.20    ack . 1024  (0.20)
		3.20    0.00   send P 1024
		3.40    0.20    ack . 1536  (0.20)
		3.80    0.40 * send . 0  (3.80) [2]
		3.82    0.02 *  ack . 1536  (0.62) [2]
	Three seconds into the conversation, bytes 512 through 1023
	were sent.  200ms later they were acked.  Shortly thereafter
	bytes 1024-1535 were sent and again acked after 200ms.
	Then, for no apparent reason, 0-511 is retransmitted, 3.8
	seconds after its initial send (the round trip time for this
	ftp was 1sec, +-500ms).  Since the receiver is expecting
	1536, 1536 is re-acked when 0 arrives.

packetdat.awk
	Computes chunk summary data for an ftp (or similar
	unidirectional tcp transfer). [A "chunk" refers to
	a chunk of the sequence space -- essentially the packet
	sequence number divided by the max segment size.]

	A summary line is printed showing the number of chunks,
	the number of packets it took to send that many chunks
	(if there are no lost or duplicated packets, the number
	of packets should equal the number of chunks) and the
	number of acks.

	Following the summary line is one line of information
	per chunk.  The line contains eight fields:
	   1 - the chunk number
	   2 - the start sequence number for this chunk
	   3 - time of first send
	   4 - time of last send
	   5 - time of first ack
	   6 - time of last ack
	   7 - number of times chunk was sent
	   8 - number of times chunk was acked
	(all times are in decimal seconds, relative to the start
	of the conversation.)

	As an example, here is the first part of the output for
	an ftp trace:

	# 134 chunks.  536 packets sent.  508 acks.
	1       1       0.00    5.80    0.20    0.20    4       1
	2       513     0.28    6.20    0.40    0.40    4       1
	3       1025    1.16    6.32    1.20    1.20    4       1
	4       1561    1.86    15.00   2.00    2.00    6       1
	5       2049    2.16    15.44   2.20    2.20    5       1
	6       2585    2.64    16.44   2.80    2.80    5       1
	7       3073    3.00    16.66   3.20    3.20    4       1
	8       3609    3.20    17.24   3.40    5.82    4       11
	9       4097    6.02    6.58    6.20    6.80    2       5

	This says that 134 chunks were transferred (about 70K
	since the average packet size was 512 bytes).  It took
	536 packets to transfer the data (i.e., on the average
	each chunk was transmitted four times).  Looking at,
	say, chunk 4, we see it represents the 512 bytes of
	sequence space from 1561 to 2048.  It was first sent
	1.86 seconds into the conversation.  It was last
	sent 15 seconds into the conversation and was sent
	a total of 6 times (i.e., it was retransmitted every
	2 seconds on the average).  It was acked once, 140ms
	after it first arrived.

stime.awk
atime.awk
	Output one line per send or ack, respectively, in the form
		<time> <seq. number>
	where <time> is the time in seconds since the start of the
	transfer and <seq. number> is the sequence number being sent
	or acked.  I typically plot this data looking for suspicious
	patterns.


The problem I was looking at was the bulk-data-transfer
throughput of medium delay network paths (1-6 sec.  round trip
time) under typical DARPA Internet conditions.  The trace of the
ftp transfer of a large file was used as the raw data source.
The method was:

  - On a local host (but not the Sun running tcpdump), connect to
    the remote ftp.

  - On the monitor Sun, start the trace going.  E.g.,
      tcpdump host local-host and remote-host and port ftp-data >tracefile

  - On local, do either a get or put of a large file (~500KB),
    preferably to the null device (to minimize effects like
    closing the receive window while waiting for a disk write).

  - When transfer is finished, stop tcpdump.  Use awk to make up
    two files of summary data (maxsize is the maximum packet size,
    tracedata is the file of tcpdump tracedata):
      awk -f send-ack.awk packetsize=avgsize tracedata >sa
      awk -f packetdat.awk packetsize=avgsize tracedata >pd

  - While the summary data files are printing, take a look at
    how the transfer behaved:
      awk -f stime.awk tracedata | xgraph
    (90% of what you learn seems to happen in this step).

  - Do all of the above steps several times, both directions,
    at different times of day, with different protocol
    implementations on the other end.

  - Using one of the Unix data analysis packages (in my case,
    S and Gary Perlman's Unix|Stat), spend a few months staring
    at the data.

  - Change something in the local protocol implementation and
    redo the steps above.

  - Once a week, tell your funding agent that you're discovering
    wonderful things and you'll write up that research report
    "real soon now".
```
