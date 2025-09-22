define(MACHINE,landisk)dnl
vers(__file__,
	{-$OpenBSD: MAKEDEV.md,v 1.52 2024/03/31 10:14:35 miod Exp $-},
etc.MACHINE)dnl
dnl
dnl Copyright (c) 2001-2004 Todd T. Fries <todd@OpenBSD.org>
dnl All rights reserved.
dnl
dnl Redistribution and use in source and binary forms, with or without
dnl modification, are permitted provided that the following conditions
dnl are met:
dnl 1. Redistributions of source code must retain the above copyright
dnl    notice, this list of conditions and the following disclaimer.
dnl 2. The name of the author may not be used to endorse or promote products
dnl    derived from this software without specific prior written permission.
dnl
dnl THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
dnl INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
dnl AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
dnl THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
dnl EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
dnl PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
dnl OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
dnl WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
dnl OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
dnl ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
dnl
dnl
dnl *** landisk specific devices
dnl
__devitem(scif, ttya, on-board serial port,scif)dnl
_mkdev(scif, ttya, {-M ttya c major_scif_c 0 660 dialer root-})dnl
dnl
dnl *** MAKEDEV itself
dnl
_TITLE(make)
_DEV(all)
_DEV(ramdisk)
_DEV(std)
_DEV(local)
_TITLE(dis)
_DEV(cd, 26, 26)
_DEV(ch, 27)
_DEV(rd, 18, 18)
_DEV(sd, 24, 24)
_DEV(vnd, 19, 19)
_DEV(wd, 16, 16)
_TITLE(tap)
_DEV(st, 25)
_TITLE(term)
_DEV(scif, 11)
_TITLE(pty)
_DEV(ptm, 98)
_DEV(pty, 5)
_DEV(tty, 4)
_TITLE(cons)
_DEV(wsdisp, 60)
_DEV(wscons)
_DEV(wskbd, 61)
_DEV(wsmouse, 62)
_DEV(wsmux, 63)
_TITLE(usb)
_DEV(ttyU, 68)
_DEV(uall)
_DEV(ugen, 70)
_DEV(uhid, 65)
_DEV(fido, 106)
_DEV(ujoy, 108)
_DEV(ulpt, 66)
_DEV(usb, 64)
_TITLE(spec)
dnl _DEV(apm, 34)
_DEV(au, 36)
_DEV(bio, 38)
dnl _DEV(bktr, 75)
_DEV(bpf, 22)
_DEV(dt, 30)
_DEV(diskmap,101)
_DEV(fdesc, 7)
_DEV(fuse, 103)
_DEV(hotplug, 37)
_DEV(pci, 88)
_DEV(pf, 46)
_DEV(pppx,102)
_DEV(pppac,107)
_DEV(radio, 97)
_DEV(rnd, 40)
_DEV(rmidi, 57)
_DEV(tun, 33)
_DEV(tap, 104)
dnl _DEV(tuner, 75)
_DEV(uk, 28)
_DEV(vi, 77)
_DEV(vscsi, 99)
_DEV(kstat, 51)
dnl
divert(__mddivert)dnl
dnl
ramdisk)
	_recurse std bpf wd0 wd1 sd0 rd0 wsmouse
	_recurse st0 ttyC0 wskbd0 bio diskmap random
	;;

_std(1, 2, 8, 6)
	;;
dnl
dnl *** landisk specific targets
dnl
twrget(all, au, audio, 0, 1, 2, 3)dnl
twrget(all, scif, tty, a)dnl
target(all, ch, 0)dnl
target(all, vscsi, 0)dnl
target(all, diskmap)dnl
twrget(all, flo, fd, 0, 0B, 0C, 0D, 0E, 0F, 0G, 0H)dnl
twrget(all, flo, fd, 1, 1B, 1C, 1D, 1E, 1F, 1G, 1H)dnl
target(all, pty, 0)dnl
target(all, bio)dnl
target(all, tun, 0, 1, 2, 3)dnl
target(all, tap, 0, 1, 2, 3)dnl
target(all, rd, 0)dnl
target(all, cd, 0, 1)dnl
target(all, sd, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9)dnl
target(all, vnd, 0, 1, 2, 3)dnl
