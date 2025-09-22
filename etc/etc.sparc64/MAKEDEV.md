define(MACHINE,sparc64)dnl
vers(__file__,
	{-$OpenBSD: MAKEDEV.md,v 1.101 2022/11/09 19:35:24 krw Exp $-},
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
dnl *** sparc64 specific definitions
dnl
__devitem(s64_tzs, tty[a-z]*, Zilog 8530 serial port,zs)dnl
__devitem(s64_czs, cua[a-z]*, Zilog 8530 serial port,zs)dnl
_mkdev(s64_tzs, {-tty[a-z]-}, {-u=${i#tty*}
	case $u in
	a) n=0 ;;
	b) n=1 ;;
	c) n=2 ;;
	d) n=3 ;;
	*) echo unknown tty device $i ;;
	esac
	M tty$u c major_s64_tzs_c $n 660 dialer root-})dnl
_mkdev(s64_czs, cua[a-z], {-u=${i#cua*}
	case $u in
	a) n=0 ;;
	b) n=1 ;;
	c) n=2 ;;
	d) n=3 ;;
	*) echo unknown cua device $i ;;
	esac
	M cua$u c major_s64_czs_c Add($n, 128) 660 dialer root-})dnl
__devitem(vcc, ttyV*, Virtual console concentrator, vcctty)dnl
_mkdev(vcc, ttyV[0-9a-zA-Z], {-U=${i#ttyV*}
	o=$(alph2d $U)
	M ttyV$U c major_vcc_c $o 600-})dnl
dnl
__devitem(uperf, uperf, Performance counters)dnl
_mkdev(uperf, uperf, {-M uperf c major_uperf_c 0 664-})dnl
dnl
__devitem(vldc_hvctl, hvctl, Hypervisor control channel, vldcp)dnl
_mkdev(vldc_hvctl, hvctl, {-M hvctl c major_vldc_hvctl_c 0 600-})dnl
__devitem(vldc_spds, spds, Service processor domain services channel, vldcp)dnl
_mkdev(vldc_spds, spds, {-M spds c major_vldc_spds_c 1 600-})dnl
__devitem(vldc_ldom, ldom*, Logical domain services channels, vldcp)dnl
_mkdev(vldc_ldom, ldom*, {-M ldom$U c major_vldc_ldom_c Add($U,32) 600-})dnl
dnl
__devitem(vdsp, vdsp*, Virtual disk server ports)dnl
_mkdev(vdsp, vdsp*, {-M vdsp$U c major_vdsp_c $U 600-})dnl
dnl
_TITLE(make)
_DEV(all)
_DEV(ramdisk)
_DEV(std)
_DEV(local)
_TITLE(dis)
_DEV(cd, 58, 18)
_DEV(flo, 54, 16)
_DEV(rd, 61, 5)
_DEV(sd, 17, 7)
_DEV(vnd, 110, 8)
_DEV(wd, 26, 12)
_TITLE(tap)
_DEV(ch, 19)
_DEV(st, 18)
_TITLE(term)
_DEV(s64_czs, 12)
_DEV(mag, 71)
_DEV(spif, 108)
_DEV(com, 36)
_DEV(s64_tzs, 12)
_DEV(tth, 77)
_DEV(vcc, 127)
_TITLE(pty)
_DEV(ptm, 123)
_DEV(pty, 21)
_DEV(tty, 20)
_TITLE(cons)
_DEV(pcons, 122)
_DEV(wsdisp, 78)
_DEV(wscons)
_DEV(wskbd, 79)
_DEV(wsmux, 81)
_TITLE(point)
_DEV(wsmouse, 80)
_TITLE(prn)
_DEV(bpp, 107)
_DEV(bppsp, 109)
_DEV(bppmag, 72)
_DEV(lpa)
_DEV(lpt, 37)
_TITLE(usb)
_DEV(ttyU, 95)
_DEV(uall)
_DEV(ugen, 92)
_DEV(uhid, 91)
_DEV(fido, 137)
_DEV(ujoy, 139)
_DEV(ulpt, 93)
_DEV(usb, 90)
_TITLE(spec)
_DEV(au, 69)
_DEV(bio, 120)
_DEV(bpf, 105)
_DEV(diskmap, 130)
_DEV(dri, 87)
_DEV(fdesc, 24)
_DEV(dt, 30)
_DEV(fuse, 134)
_DEV(hotplug, 124)
_DEV(oppr)
_DEV(pci, 52)
_DEV(pf, 73)
_DEV(pppx, 131)
_DEV(pppac, 138)
_DEV(rmidi, 68)
_DEV(rnd, 119)
_DEV(tun, 111)
_DEV(tap, 135)
_DEV(uk, 60)
_DEV(uperf, 25)
_DEV(vi, 44)
_DEV(vscsi, 128)
_DEV(vldc_hvctl, 132)
_DEV(vldc_spds, 132)
_DEV(vldc_ldom, 132)
_DEV(vdsp, 133)
_DEV(kstat, 51)
dnl
divert(__mddivert)dnl
dnl
ramdisk)
	_recurse std fd0 wd0 wd1 wd2 sd0 sd1 sd2 rd0
	_recurse st0 cd0 bpf bio diskmap random
	;;

_std(2, 3, 76, 16)
	M openprom	c 70 0 600
	M mdesc		c 70 1 640 kmem
	M pri		c 70 2 640 kmem
	M vcons0	c 125 0 600
	;;
dnl
dnl *** sparc64 specific targets
dnl
twrget(wscons, wscons, ttyD, cfg, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b)dnl
twrget(wscons, wscons, ttyE, cfg, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b)dnl
twrget(wscons, wscons, ttyF, cfg, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b)dnl
twrget(wscons, wscons, ttyG, cfg, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b)dnl
twrget(wscons, wscons, ttyH, cfg, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b)dnl
twrget(wscons, wscons, ttyI, cfg, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b)dnl
twrget(wscons, wscons, ttyJ, cfg, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b)dnl
twrget(all, au, audio, 0, 1, 2, 3)dnl
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
target(all, bpp, 0)dnl
target(all, dri)dnl
twrget(all, s64_tzs, tty, a, b, c, d)dnl
twrget(all, s64_czs, cua, a, b, c, d)dnl
twrget(all, vcc, ttyV, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f)dnl
twrget(all, vldc_hvctl, hvctl)dnl
twrget(all, vldc_spds, spds)dnl
twrget(all, vldc_ldom, ldom, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)dnl
target(all, vdsp, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23)dnl
