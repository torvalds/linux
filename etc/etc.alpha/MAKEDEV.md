define(MACHINE,alpha)dnl
vers(__file__,
	{-$OpenBSD: MAKEDEV.md,v 1.78 2021/11/11 09:47:32 claudio Exp $-},
etc.MACHINE)dnl
dnl
dnl Copyright (c) 2001-2006 Todd T. Fries <todd@OpenBSD.org>
dnl
dnl Permission to use, copy, modify, and distribute this software for any
dnl purpose with or without fee is hereby granted, provided that the above
dnl copyright notice and this permission notice appear in all copies.
dnl
dnl THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
dnl WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
dnl MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
dnl ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
dnl WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
dnl ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
dnl OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
dnl
_TITLE(make)
_DEV(all)
_DEV(ramdisk)
_DEV(std)
_DEV(local)
_TITLE(dis)
_DEV(cd, 13, 3)
_DEV(flo, 37, 4)
_DEV(rd, 28, 6)
_DEV(sd, 8, 8)
_DEV(vnd, 9, 9)
_DEV(wd, 36, 0)
_TITLE(tap)
_DEV(ch, 14)
_DEV(st, 12)
_TITLE(term)
_DEV(com, 26)
_DEV(ttyc, 38)
_DEV(ttyB, 15)
_TITLE(pty)
_DEV(ptm, 55)
_DEV(pty, 5)
_DEV(tty, 4)
_TITLE(cons)
_DEV(wsdisp, 25)
_DEV(wscons)
_DEV(wskbd, 29)
_DEV(wsmux, 60)
_TITLE(point)
_DEV(wsmouse, 30)
_TITLE(prn)
_DEV(lpa)
_DEV(lpt, 31)
_TITLE(usb)
_DEV(ttyU, 49)
_DEV(uall)
_DEV(ugen, 48)
_DEV(uhid, 46)
_DEV(fido, 70)
_DEV(ujoy, 72)
_DEV(ulpt, 47)
_DEV(usb, 45)
_TITLE(spec)
_DEV(au, 24)
_DEV(bio, 53)
_DEV(bktr, 58)
_DEV(bpf, 11)
_DEV(diskmap, 63)
_DEV(dt, 32)
_DEV(fdesc, 10)
_DEV(fuse, 67)
_DEV(hotplug, 56)
_DEV(pci, 52)
_DEV(pf, 35)
_DEV(pppx, 64)
_DEV(pppac, 71)
_DEV(radio, 59)
_DEV(rnd, 34)
_DEV(rmidi, 41)
_DEV(speak, 40)
_DEV(tun, 7)
_DEV(tap, 68)
_DEV(tuner, 58)
_DEV(uk, 33)
_DEV(vi, 44)
_DEV(vscsi, 61)
_DEV(kstat, 51)
dnl
divert(__mddivert)dnl
dnl
ramdisk)
	_recurse std fd0 wd0 wd1 wd2 sd0 sd1 sd2 bpf
	_recurse st0 cd0 ttyC0 rd0 bio diskmap random
	;;

_std(1, 2, 39, 6)
	M xf86		c 2 4 600
	;;

ttyB*|ttyc*)
	U=${i##tty?}
	case $i in
	ttyB*)	type=B major=15 minor=$U;;
	ttyc*)	type=c major=38 minor=$U;;
	esac
	M tty$type$U c $major $minor 660 dialer root
	M cua$type$U c $major Add($minor, 128) 660 dialer root
	;;
dnl
dnl *** alpha specific targets
dnl
twrget(all, au, audio, 0, 1, 2, 3)dnl
target(all, bio)dnl
target(all, ch, 0)dnl
target(all, vscsi, 0)dnl
target(all, diskmap)dnl
twrget(all, flo, fd, 0, 0B, 0C, 0D, 0E, 0F, 0G, 0H)dnl
twrget(all, flo, fd, 1, 1B, 1C, 1D, 1E, 1F, 1G, 1H)dnl
target(all, pty, 0)dnl
target(all, tun, 0, 1, 2, 3)dnl
target(all, tap, 0, 1, 2, 3)dnl
target(all, ttyB, 0, 1)dnl
target(all, rd, 0)dnl
target(all, cd, 0, 1)dnl
target(all, sd, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9)dnl
target(all, vnd, 0, 1, 2, 3)dnl
target(ramd, ttyB, 0, 1)dnl
