/*	video.S
 *
 *	Display adapter & video mode setup, version 2.13 (14-May-99)
 *
 *	Copyright (C) 1995 -- 1998 Martin Mares <mj@ucw.cz>
 *	Based on the original setup.S code (C) Linus Torvalds and Mats Anderson
 *
 *	Rewritten to use GNU 'as' by Chris Noe <stiker@northlink.com> May 1999
 *
 *	For further information, look at Documentation/svga.txt.
 *
 */

/* Enable autodetection of SVGA adapters and modes. */
#undef CONFIG_VIDEO_SVGA

/* Enable autodetection of VESA modes */
#define CONFIG_VIDEO_VESA

/* Enable compacting of mode table */
#define CONFIG_VIDEO_COMPACT

/* Retain screen contents when switching modes */
#define CONFIG_VIDEO_RETAIN

/* Enable local mode list */
#undef CONFIG_VIDEO_LOCAL

/* Force 400 scan lines for standard modes (hack to fix bad BIOS behaviour */
#undef CONFIG_VIDEO_400_HACK

/* Hack that lets you force specific BIOS mode ID and specific dimensions */
#undef CONFIG_VIDEO_GFX_HACK
#define VIDEO_GFX_BIOS_AX 0x4f02	/* 800x600 on ThinkPad */
#define VIDEO_GFX_BIOS_BX 0x0102
#define VIDEO_GFX_DUMMY_RESOLUTION 0x6425	/* 100x37 */

/* This code uses an extended set of video mode numbers. These include:
 * Aliases for standard modes
 *	NORMAL_VGA (-1)
 *	EXTENDED_VGA (-2)
 *	ASK_VGA (-3)
 * Video modes numbered by menu position -- NOT RECOMMENDED because of lack
 * of compatibility when extending the table. These are between 0x00 and 0xff.
 */
#define VIDEO_FIRST_MENU 0x0000

/* Standard BIOS video modes (BIOS number + 0x0100) */
#define VIDEO_FIRST_BIOS 0x0100

/* VESA BIOS video modes (VESA number + 0x0200) */
#define VIDEO_FIRST_VESA 0x0200

/* Video7 special modes (BIOS number + 0x0900) */
#define VIDEO_FIRST_V7 0x0900

/* Special video modes */
#define VIDEO_FIRST_SPECIAL 0x0f00
#define VIDEO_80x25 0x0f00
#define VIDEO_8POINT 0x0f01
#define VIDEO_80x43 0x0f02
#define VIDEO_80x28 0x0f03
#define VIDEO_CURRENT_MODE 0x0f04
#define VIDEO_80x30 0x0f05
#define VIDEO_80x34 0x0f06
#define VIDEO_80x60 0x0f07
#define VIDEO_GFX_HACK 0x0f08
#define VIDEO_LAST_SPECIAL 0x0f09

/* Video modes given by resolution */
#define VIDEO_FIRST_RESOLUTION 0x1000

/* The "recalculate timings" flag */
#define VIDEO_RECALC 0x8000

/* Positions of various video parameters passed to the kernel */
/* (see also include/linux/tty.h) */
#define PARAM_CURSOR_POS	0x00
#define PARAM_VIDEO_PAGE	0x04
#define PARAM_VIDEO_MODE	0x06
#define PARAM_VIDEO_COLS	0x07
#define PARAM_VIDEO_EGA_BX	0x0a
#define PARAM_VIDEO_LINES	0x0e
#define PARAM_HAVE_VGA		0x0f
#define PARAM_FONT_POINTS	0x10

#define PARAM_LFB_WIDTH		0x12
#define PARAM_LFB_HEIGHT	0x14
#define PARAM_LFB_DEPTH		0x16
#define PARAM_LFB_BASE		0x18
#define PARAM_LFB_SIZE		0x1c
#define PARAM_LFB_LINELENGTH	0x24
#define PARAM_LFB_COLORS	0x26
#define PARAM_VESAPM_SEG	0x2e
#define PARAM_VESAPM_OFF	0x30
#define PARAM_LFB_PAGES		0x32
#define PARAM_VESA_ATTRIB	0x34
#define PARAM_CAPABILITIES	0x36

/* Define DO_STORE according to CONFIG_VIDEO_RETAIN */
#ifdef CONFIG_VIDEO_RETAIN
#define DO_STORE call store_screen
#else
#define DO_STORE
#endif /* CONFIG_VIDEO_RETAIN */

# This is the main entry point called by setup.S
# %ds *must* be pointing to the bootsector
video:	pushw	%ds		# We use different segments
	pushw	%ds		# FS contains original DS
	popw	%fs
	pushw	%cs		# DS is equal to CS
	popw	%ds
	pushw	%cs		# ES is equal to CS
	popw	%es
	xorw	%ax, %ax
	movw	%ax, %gs	# GS is zero
	cld
	call	basic_detect	# Basic adapter type testing (EGA/VGA/MDA/CGA)
#ifdef CONFIG_VIDEO_SELECT
	movw	%fs:(0x01fa), %ax		# User selected video mode
	cmpw	$ASK_VGA, %ax			# Bring up the menu
	jz	vid2

	call	mode_set			# Set the mode
	jc	vid1

	leaw	badmdt, %si			# Invalid mode ID
	call	prtstr
vid2:	call	mode_menu
vid1:
#ifdef CONFIG_VIDEO_RETAIN
	call	restore_screen			# Restore screen contents
#endif /* CONFIG_VIDEO_RETAIN */
	call	store_edid
#endif /* CONFIG_VIDEO_SELECT */
	call	mode_params			# Store mode parameters
	popw	%ds				# Restore original DS
	ret

# Detect if we have CGA, MDA, EGA or VGA and pass it to the kernel.
basic_detect:
	movb	$0, %fs:(PARAM_HAVE_VGA)
	movb	$0x12, %ah	# Check EGA/VGA
	movb	$0x10, %bl
	int	$0x10
	movw	%bx, %fs:(PARAM_VIDEO_EGA_BX)	# Identifies EGA to the kernel
	cmpb	$0x10, %bl			# No, it's a CGA/MDA/HGA card.
	je	basret

	incb	adapter
	movw	$0x1a00, %ax			# Check EGA or VGA?
	int	$0x10
	cmpb	$0x1a, %al			# 1a means VGA...
	jne	basret				# anything else is EGA.
	
	incb	%fs:(PARAM_HAVE_VGA)		# We've detected a VGA
	incb	adapter
basret:	ret

# Store the video mode parameters for later usage by the kernel.
# This is done by asking the BIOS except for the rows/columns
# parameters in the default 80x25 mode -- these are set directly,
# because some very obscure BIOSes supply insane values.
mode_params:
#ifdef CONFIG_VIDEO_SELECT
	cmpb	$0, graphic_mode
	jnz	mopar_gr
#endif
	movb	$0x03, %ah			# Read cursor position
	xorb	%bh, %bh
	int	$0x10
	movw	%dx, %fs:(PARAM_CURSOR_POS)
	movb	$0x0f, %ah			# Read page/mode/width
	int	$0x10
	movw	%bx, %fs:(PARAM_VIDEO_PAGE)
	movw	%ax, %fs:(PARAM_VIDEO_MODE)	# Video mode and screen width
	cmpb	$0x7, %al			# MDA/HGA => segment differs
	jnz	mopar0

	movw	$0xb000, video_segment
mopar0: movw	%gs:(0x485), %ax		# Font size
	movw	%ax, %fs:(PARAM_FONT_POINTS)	# (valid only on EGA/VGA)
	movw	force_size, %ax			# Forced size?
	orw	%ax, %ax
	jz	mopar1

	movb	%ah, %fs:(PARAM_VIDEO_COLS)
	movb	%al, %fs:(PARAM_VIDEO_LINES)
	ret

mopar1:	movb	$25, %al
	cmpb	$0, adapter			# If we are on CGA/MDA/HGA, the
	jz	mopar2				# screen must have 25 lines.

	movb	%gs:(0x484), %al		# On EGA/VGA, use the EGA+ BIOS
	incb	%al				# location of max lines.
mopar2: movb	%al, %fs:(PARAM_VIDEO_LINES)
	ret

#ifdef CONFIG_VIDEO_SELECT
# Fetching of VESA frame buffer parameters
mopar_gr:
	leaw	modelist+1024, %di
	movb	$0x23, %fs:(PARAM_HAVE_VGA)
	movw	16(%di), %ax
	movw	%ax, %fs:(PARAM_LFB_LINELENGTH)
	movw	18(%di), %ax
	movw	%ax, %fs:(PARAM_LFB_WIDTH)
	movw	20(%di), %ax
	movw	%ax, %fs:(PARAM_LFB_HEIGHT)
	movb	25(%di), %al
	movb	$0, %ah
	movw	%ax, %fs:(PARAM_LFB_DEPTH)
	movb	29(%di), %al	
	movb	$0, %ah
	movw	%ax, %fs:(PARAM_LFB_PAGES)
	movl	40(%di), %eax
	movl	%eax, %fs:(PARAM_LFB_BASE)
	movl	31(%di), %eax
	movl	%eax, %fs:(PARAM_LFB_COLORS)
	movl	35(%di), %eax
	movl	%eax, %fs:(PARAM_LFB_COLORS+4)
	movw	0(%di), %ax
	movw	%ax, %fs:(PARAM_VESA_ATTRIB)

# get video mem size
	leaw	modelist+1024, %di
	movw	$0x4f00, %ax
	int	$0x10
	xorl	%eax, %eax
	movw	18(%di), %ax
	movl	%eax, %fs:(PARAM_LFB_SIZE)

# store mode capabilities
	movl 10(%di), %eax
	movl %eax, %fs:(PARAM_CAPABILITIES)

# switching the DAC to 8-bit is for <= 8 bpp only
	movw	%fs:(PARAM_LFB_DEPTH), %ax
	cmpw	$8, %ax
	jg	dac_done

# get DAC switching capability
	xorl	%eax, %eax
	movb	10(%di), %al
	testb	$1, %al
	jz	dac_set

# attempt to switch DAC to 8-bit
	movw	$0x4f08, %ax
	movw	$0x0800, %bx
	int	$0x10
	cmpw	$0x004f, %ax
	jne     dac_set
	movb    %bh, dac_size		# store actual DAC size

dac_set:
# set color size to DAC size
	movb	dac_size, %al
	movb	%al, %fs:(PARAM_LFB_COLORS+0)
	movb	%al, %fs:(PARAM_LFB_COLORS+2)
	movb	%al, %fs:(PARAM_LFB_COLORS+4)
	movb	%al, %fs:(PARAM_LFB_COLORS+6)

# set color offsets to 0
	movb	$0, %fs:(PARAM_LFB_COLORS+1)
	movb	$0, %fs:(PARAM_LFB_COLORS+3)
	movb	$0, %fs:(PARAM_LFB_COLORS+5)
	movb	$0, %fs:(PARAM_LFB_COLORS+7)

dac_done:
# get protected mode interface informations
	movw	$0x4f0a, %ax
	xorw	%bx, %bx
	xorw	%di, %di
	int	$0x10
	cmp	$0x004f, %ax
	jnz	no_pm

	movw	%es, %fs:(PARAM_VESAPM_SEG)
	movw	%di, %fs:(PARAM_VESAPM_OFF)
no_pm:	ret

# The video mode menu
mode_menu:
	leaw	keymsg, %si			# "Return/Space/Timeout" message
	call	prtstr
	call	flush
nokey:	call	getkt

	cmpb	$0x0d, %al			# ENTER ?
	je	listm				# yes - manual mode selection

	cmpb	$0x20, %al			# SPACE ?
	je	defmd1				# no - repeat

	call 	beep
	jmp	nokey

defmd1:	ret					# No mode chosen? Default 80x25

listm:	call	mode_table			# List mode table
listm0:	leaw	name_bann, %si			# Print adapter name
	call	prtstr
	movw	card_name, %si
	orw	%si, %si
	jnz	an2

	movb	adapter, %al
	leaw	old_name, %si
	orb	%al, %al
	jz	an1

	leaw	ega_name, %si
	decb	%al
	jz	an1

	leaw	vga_name, %si
	jmp	an1

an2:	call	prtstr
	leaw	svga_name, %si
an1:	call	prtstr
	leaw	listhdr, %si			# Table header
	call	prtstr
	movb	$0x30, %dl			# DL holds mode number
	leaw	modelist, %si
lm1:	cmpw	$ASK_VGA, (%si)			# End?
	jz	lm2

	movb	%dl, %al			# Menu selection number
	call	prtchr
	call	prtsp2
	lodsw
	call	prthw				# Mode ID
	call	prtsp2
	movb	0x1(%si), %al
	call	prtdec				# Rows
	movb	$0x78, %al			# the letter 'x'
	call	prtchr
	lodsw
	call	prtdec				# Columns
	movb	$0x0d, %al			# New line
	call	prtchr
	movb	$0x0a, %al
	call	prtchr
	incb	%dl				# Next character
	cmpb	$0x3a, %dl
	jnz	lm1

	movb	$0x61, %dl
	jmp	lm1

lm2:	leaw	prompt, %si			# Mode prompt
	call	prtstr
	leaw	edit_buf, %di			# Editor buffer
lm3:	call	getkey
	cmpb	$0x0d, %al			# Enter?
	jz	lment

	cmpb	$0x08, %al			# Backspace?
	jz	lmbs

	cmpb	$0x20, %al			# Printable?
	jc	lm3

	cmpw	$edit_buf+4, %di		# Enough space?
	jz	lm3

	stosb
	call	prtchr
	jmp	lm3

lmbs:	cmpw	$edit_buf, %di			# Backspace
	jz	lm3

	decw	%di
	movb	$0x08, %al
	call	prtchr
	call	prtspc
	movb	$0x08, %al
	call	prtchr
	jmp	lm3
	
lment:	movb	$0, (%di)
	leaw	crlft, %si
	call	prtstr
	leaw	edit_buf, %si
	cmpb	$0, (%si)			# Empty string = default mode
	jz	lmdef

	cmpb	$0, 1(%si)			# One character = menu selection
	jz	mnusel

	cmpw	$0x6373, (%si)			# "scan" => mode scanning
	jnz	lmhx

	cmpw	$0x6e61, 2(%si)
	jz	lmscan

lmhx:	xorw	%bx, %bx			# Else => mode ID in hex
lmhex:	lodsb
	orb	%al, %al
	jz	lmuse1

	subb	$0x30, %al
	jc	lmbad

	cmpb	$10, %al
	jc	lmhx1

	subb	$7, %al
	andb	$0xdf, %al
	cmpb	$10, %al
	jc	lmbad

	cmpb	$16, %al
	jnc	lmbad

lmhx1:	shlw	$4, %bx
	orb	%al, %bl
	jmp	lmhex

lmuse1:	movw	%bx, %ax
	jmp	lmuse

mnusel:	lodsb					# Menu selection
	xorb	%ah, %ah
	subb	$0x30, %al
	jc	lmbad

	cmpb	$10, %al
	jc	lmuse
	
	cmpb	$0x61-0x30, %al
	jc	lmbad
	
	subb	$0x61-0x30-10, %al
	cmpb	$36, %al
	jnc	lmbad

lmuse:	call	mode_set
	jc	lmdef

lmbad:	leaw	unknt, %si
	call	prtstr
	jmp	lm2
lmscan:	cmpb	$0, adapter			# Scanning only on EGA/VGA
	jz	lmbad

	movw	$0, mt_end			# Scanning of modes is
	movb	$1, scanning			# done as new autodetection.
	call	mode_table
	jmp	listm0
lmdef:	ret

# Additional parts of mode_set... (relative jumps, you know)
setv7:						# Video7 extended modes
	DO_STORE
	subb	$VIDEO_FIRST_V7>>8, %bh
	movw	$0x6f05, %ax
	int	$0x10
	stc
	ret

_setrec:	jmp	setrec			# Ugly...
_set_80x25:	jmp	set_80x25

# Aliases for backward compatibility.
setalias:
	movw	$VIDEO_80x25, %ax
	incw	%bx
	jz	mode_set

	movb	$VIDEO_8POINT-VIDEO_FIRST_SPECIAL, %al
	incw	%bx
	jnz	setbad				# Fall-through!

# Setting of user mode (AX=mode ID) => CF=success
mode_set:
	movw	%ax, %fs:(0x01fa)		# Store mode for use in acpi_wakeup.S
	movw	%ax, %bx
	cmpb	$0xff, %ah
	jz	setalias

	testb	$VIDEO_RECALC>>8, %ah
	jnz	_setrec

	cmpb	$VIDEO_FIRST_RESOLUTION>>8, %ah
	jnc	setres
	
	cmpb	$VIDEO_FIRST_SPECIAL>>8, %ah
	jz	setspc
	
	cmpb	$VIDEO_FIRST_V7>>8, %ah
	jz	setv7
	
	cmpb	$VIDEO_FIRST_VESA>>8, %ah
	jnc	check_vesa
	
	orb	%ah, %ah
	jz	setmenu
	
	decb	%ah
	jz	setbios

setbad:	clc
	movb	$0, do_restore			# The screen needn't be restored
	ret

setvesa:
	DO_STORE
	subb	$VIDEO_FIRST_VESA>>8, %bh
	movw	$0x4f02, %ax			# VESA BIOS mode set call
	int	$0x10
	cmpw	$0x004f, %ax			# AL=4f if implemented
	jnz	setbad				# AH=0 if OK

	stc
	ret

setbios:
	DO_STORE
	int	$0x10				# Standard BIOS mode set call
	pushw	%bx
	movb	$0x0f, %ah			# Check if really set
	int	$0x10
	popw	%bx
	cmpb	%bl, %al
	jnz	setbad
	
	stc
	ret

setspc:	xorb	%bh, %bh			# Set special mode
	cmpb	$VIDEO_LAST_SPECIAL-VIDEO_FIRST_SPECIAL, %bl
	jnc	setbad
	
	addw	%bx, %bx
	jmp	*spec_inits(%bx)

setmenu:
	orb	%al, %al			# 80x25 is an exception
	jz	_set_80x25
	
	pushw	%bx				# Set mode chosen from menu
	call	mode_table			# Build the mode table
	popw	%ax
	shlw	$2, %ax
	addw	%ax, %si
	cmpw	%di, %si
	jnc	setbad
	
	movw	(%si), %ax			# Fetch mode ID
_m_s:	jmp	mode_set

setres:	pushw	%bx				# Set mode chosen by resolution
	call	mode_table
	popw	%bx
	xchgb	%bl, %bh
setr1:	lodsw
	cmpw	$ASK_VGA, %ax			# End of the list?
	jz	setbad
	
	lodsw
	cmpw	%bx, %ax
	jnz	setr1
	
	movw	-4(%si), %ax			# Fetch mode ID
	jmp	_m_s

check_vesa:
#ifdef CONFIG_FIRMWARE_EDID
	leaw	modelist+1024, %di
	movw	$0x4f00, %ax
	int	$0x10
	cmpw	$0x004f, %ax
	jnz	setbad

	movw	4(%di), %ax
	movw	%ax, vbe_version
#endif
	leaw	modelist+1024, %di
	subb	$VIDEO_FIRST_VESA>>8, %bh
	movw	%bx, %cx			# Get mode information structure
	movw	$0x4f01, %ax
	int	$0x10
	addb	$VIDEO_FIRST_VESA>>8, %bh
	cmpw	$0x004f, %ax
	jnz	setbad

	movb	(%di), %al			# Check capabilities.
	andb	$0x19, %al
	cmpb	$0x09, %al
	jz	setvesa				# This is a text mode

	movb	(%di), %al			# Check capabilities.
	andb	$0x99, %al
	cmpb	$0x99, %al
	jnz	_setbad				# Doh! No linear frame buffer.

	subb	$VIDEO_FIRST_VESA>>8, %bh
	orw	$0x4000, %bx			# Use linear frame buffer
	movw	$0x4f02, %ax			# VESA BIOS mode set call
	int	$0x10
	cmpw	$0x004f, %ax			# AL=4f if implemented
	jnz	_setbad				# AH=0 if OK

	movb	$1, graphic_mode		# flag graphic mode
	movb	$0, do_restore			# no screen restore
	stc
	ret

_setbad:	jmp	setbad          	# Ugly...

# Recalculate vertical display end registers -- this fixes various
# inconsistencies of extended modes on many adapters. Called when
# the VIDEO_RECALC flag is set in the mode ID.

setrec:	subb	$VIDEO_RECALC>>8, %ah		# Set the base mode
	call	mode_set
	jnc	rct3

	movw	%gs:(0x485), %ax		# Font size in pixels
	movb	%gs:(0x484), %bl		# Number of rows
	incb	%bl
	mulb	%bl				# Number of visible
	decw	%ax				# scan lines - 1
	movw	$0x3d4, %dx
	movw	%ax, %bx
	movb	$0x12, %al			# Lower 8 bits
	movb	%bl, %ah
	outw	%ax, %dx
	movb	$0x07, %al		# Bits 8 and 9 in the overflow register
	call	inidx
	xchgb	%al, %ah
	andb	$0xbd, %ah
	shrb	%bh
	jnc	rct1
	orb	$0x02, %ah
rct1:	shrb	%bh
	jnc	rct2
	orb	$0x40, %ah
rct2:	movb	$0x07, %al
	outw	%ax, %dx
	stc
rct3:	ret

# Table of routines for setting of the special modes.
spec_inits:
	.word	set_80x25
	.word	set_8pixel
	.word	set_80x43
	.word	set_80x28
	.word	set_current
	.word	set_80x30
	.word	set_80x34
	.word	set_80x60
	.word	set_gfx

# Set the 80x25 mode. If already set, do nothing.
set_80x25:
	movw	$0x5019, force_size		# Override possibly broken BIOS
use_80x25:
#ifdef CONFIG_VIDEO_400_HACK
	movw	$0x1202, %ax			# Force 400 scan lines
	movb	$0x30, %bl
	int	$0x10
#else
	movb	$0x0f, %ah			# Get current mode ID
	int	$0x10
	cmpw	$0x5007, %ax	# Mode 7 (80x25 mono) is the only one available
	jz	st80		# on CGA/MDA/HGA and is also available on EGAM

	cmpw	$0x5003, %ax	# Unknown mode, force 80x25 color
	jnz	force3

st80:	cmpb	$0, adapter	# CGA/MDA/HGA => mode 3/7 is always 80x25
	jz	set80

	movb	%gs:(0x0484), %al	# This is EGA+ -- beware of 80x50 etc.
	orb	%al, %al		# Some buggy BIOS'es set 0 rows
	jz	set80
	
	cmpb	$24, %al		# It's hopefully correct
	jz	set80
#endif /* CONFIG_VIDEO_400_HACK */
force3:	DO_STORE
	movw	$0x0003, %ax			# Forced set
	int	$0x10
set80:	stc
	ret

# Set the 80x50/80x43 8-pixel mode. Simple BIOS calls.
set_8pixel:
	DO_STORE
	call	use_80x25			# The base is 80x25
set_8pt:
	movw	$0x1112, %ax			# Use 8x8 font
	xorb	%bl, %bl
	int	$0x10
	movw	$0x1200, %ax			# Use alternate print screen
	movb	$0x20, %bl
	int	$0x10
	movw	$0x1201, %ax			# Turn off cursor emulation
	movb	$0x34, %bl
	int	$0x10
	movb	$0x01, %ah			# Define cursor scan lines 6-7
	movw	$0x0607, %cx
	int	$0x10
set_current:
	stc
	ret

# Set the 80x28 mode. This mode works on all VGA's, because it's a standard
# 80x25 mode with 14-point fonts instead of 16-point.
set_80x28:
	DO_STORE
	call	use_80x25			# The base is 80x25
set14:	movw	$0x1111, %ax			# Use 9x14 font
	xorb	%bl, %bl
	int	$0x10
	movb	$0x01, %ah			# Define cursor scan lines 11-12
	movw	$0x0b0c, %cx
	int	$0x10
	stc
	ret

# Set the 80x43 mode. This mode is works on all VGA's.
# It's a 350-scanline mode with 8-pixel font.
set_80x43:
	DO_STORE
	movw	$0x1201, %ax			# Set 350 scans
	movb	$0x30, %bl
	int	$0x10
	movw	$0x0003, %ax			# Reset video mode
	int	$0x10
	jmp	set_8pt				# Use 8-pixel font

# Set the 80x30 mode (all VGA's). 480 scanlines, 16-pixel font.
set_80x30:
	call	use_80x25			# Start with real 80x25
	DO_STORE
	movw	$0x3cc, %dx			# Get CRTC port
	inb	%dx, %al
	movb	$0xd4, %dl
	rorb	%al				# Mono or color?
	jc	set48a

	movb	$0xb4, %dl
set48a:	movw	$0x0c11, %ax		# Vertical sync end (also unlocks CR0-7)
 	call	outidx
	movw	$0x0b06, %ax			# Vertical total
 	call	outidx
	movw	$0x3e07, %ax			# (Vertical) overflow
 	call	outidx
	movw	$0xea10, %ax			# Vertical sync start
 	call	outidx
	movw	$0xdf12, %ax			# Vertical display end
	call	outidx
	movw	$0xe715, %ax			# Vertical blank start
 	call	outidx
	movw	$0x0416, %ax			# Vertical blank end
 	call	outidx
	pushw	%dx
	movb	$0xcc, %dl			# Misc output register (read)
 	inb	%dx, %al
 	movb	$0xc2, %dl			# (write)
 	andb	$0x0d, %al	# Preserve clock select bits and color bit
 	orb	$0xe2, %al			# Set correct sync polarity
 	outb	%al, %dx
	popw	%dx
	movw	$0x501e, force_size
	stc					# That's all.
	ret

# Set the 80x34 mode (all VGA's). 480 scans, 14-pixel font.
set_80x34:
	call	set_80x30			# Set 480 scans
	call	set14				# And 14-pt font
	movw	$0xdb12, %ax			# VGA vertical display end
	movw	$0x5022, force_size
setvde:	call	outidx
	stc
	ret

# Set the 80x60 mode (all VGA's). 480 scans, 8-pixel font.
set_80x60:
	call	set_80x30			# Set 480 scans
	call	set_8pt				# And 8-pt font
	movw	$0xdf12, %ax			# VGA vertical display end
	movw	$0x503c, force_size
	jmp	setvde

# Special hack for ThinkPad graphics
set_gfx:
#ifdef CONFIG_VIDEO_GFX_HACK
	movw	$VIDEO_GFX_BIOS_AX, %ax
	movw	$VIDEO_GFX_BIOS_BX, %bx
	int	$0x10
	movw	$VIDEO_GFX_DUMMY_RESOLUTION, force_size
	stc
#endif
	ret

#ifdef CONFIG_VIDEO_RETAIN

# Store screen contents to temporary buffer.
store_screen:
	cmpb	$0, do_restore			# Already stored?
	jnz	stsr

	testb	$CAN_USE_HEAP, loadflags	# Have we space for storing?
	jz	stsr
	
	pushw	%ax
	pushw	%bx
	pushw	force_size			# Don't force specific size
	movw	$0, force_size
	call	mode_params			# Obtain params of current mode
	popw	force_size
	movb	%fs:(PARAM_VIDEO_LINES), %ah
	movb	%fs:(PARAM_VIDEO_COLS), %al
	movw	%ax, %bx			# BX=dimensions
	mulb	%ah
	movw	%ax, %cx			# CX=number of characters
	addw	%ax, %ax			# Calculate image size
	addw	$modelist+1024+4, %ax
	cmpw	heap_end_ptr, %ax
	jnc	sts1				# Unfortunately, out of memory

	movw	%fs:(PARAM_CURSOR_POS), %ax	# Store mode params
	leaw	modelist+1024, %di
	stosw
	movw	%bx, %ax
	stosw
	pushw	%ds				# Store the screen
	movw	video_segment, %ds
	xorw	%si, %si
	rep
	movsw
	popw	%ds
	incb	do_restore			# Screen will be restored later
sts1:	popw	%bx
	popw	%ax
stsr:	ret

# Restore screen contents from temporary buffer.
restore_screen:
	cmpb	$0, do_restore			# Has the screen been stored?
	jz	res1

	call	mode_params			# Get parameters of current mode
	movb	%fs:(PARAM_VIDEO_LINES), %cl
	movb	%fs:(PARAM_VIDEO_COLS), %ch
	leaw	modelist+1024, %si		# Screen buffer
	lodsw					# Set cursor position
	movw	%ax, %dx
	cmpb	%cl, %dh
	jc	res2
	
	movb	%cl, %dh
	decb	%dh
res2:	cmpb	%ch, %dl
	jc	res3
	
	movb	%ch, %dl
	decb	%dl
res3:	movb	$0x02, %ah
	movb	$0x00, %bh
	int	$0x10
	lodsw					# Display size
	movb	%ah, %dl			# DL=number of lines
	movb	$0, %ah				# BX=phys. length of orig. line
	movw	%ax, %bx
	cmpb	%cl, %dl			# Too many?
	jc	res4

	pushw	%ax
	movb	%dl, %al
	subb	%cl, %al
	mulb	%bl
	addw	%ax, %si
	addw	%ax, %si
	popw	%ax
	movb	%cl, %dl
res4:	cmpb	%ch, %al			# Too wide?
	jc	res5
	
	movb	%ch, %al			# AX=width of src. line
res5:	movb	$0, %cl
	xchgb	%ch, %cl
	movw	%cx, %bp			# BP=width of dest. line
	pushw	%es
	movw	video_segment, %es
	xorw	%di, %di			# Move the data
	addw	%bx, %bx			# Convert BX and BP to _bytes_
	addw	%bp, %bp
res6:	pushw	%si
	pushw	%di
	movw	%ax, %cx
	rep
	movsw
	popw	%di
	popw	%si
	addw	%bp, %di
	addw	%bx, %si
	decb	%dl
	jnz	res6
	
	popw	%es				# Done
res1:	ret
#endif /* CONFIG_VIDEO_RETAIN */

# Write to indexed VGA register (AL=index, AH=data, DX=index reg. port)
outidx:	outb	%al, %dx
	pushw	%ax
	movb	%ah, %al
	incw	%dx
	outb	%al, %dx
	decw	%dx
	popw	%ax
	ret

# Build the table of video modes (stored after the setup.S code at the
# `modelist' label. Each video mode record looks like:
#	.word	MODE-ID		(our special mode ID (see above))
#	.byte	rows		(number of rows)
#	.byte	columns		(number of columns)
# Returns address of the end of the table in DI, the end is marked
# with a ASK_VGA ID.
mode_table:
	movw	mt_end, %di			# Already filled?
	orw	%di, %di
	jnz	mtab1x
	
	leaw	modelist, %di			# Store standard modes:
	movl	$VIDEO_80x25 + 0x50190000, %eax	# The 80x25 mode (ALL)
	stosl
	movb	adapter, %al			# CGA/MDA/HGA -- no more modes
	orb	%al, %al
	jz	mtabe
	
	decb	%al
	jnz	mtabv
	
	movl	$VIDEO_8POINT + 0x502b0000, %eax	# The 80x43 EGA mode
	stosl
	jmp	mtabe

mtab1x:	jmp	mtab1

mtabv:	leaw	vga_modes, %si			# All modes for std VGA
	movw	$vga_modes_end-vga_modes, %cx
	rep	# I'm unable to use movsw as I don't know how to store a half
	movsb	# of the expression above to cx without using explicit shr.

	cmpb	$0, scanning			# Mode scan requested?
	jz	mscan1
	
	call	mode_scan
mscan1:

#ifdef CONFIG_VIDEO_LOCAL
	call	local_modes
#endif /* CONFIG_VIDEO_LOCAL */

#ifdef CONFIG_VIDEO_VESA
	call	vesa_modes			# Detect VESA VGA modes
#endif /* CONFIG_VIDEO_VESA */

#ifdef CONFIG_VIDEO_SVGA
	cmpb	$0, scanning			# Bypass when scanning
	jnz	mscan2
	
	call	svga_modes			# Detect SVGA cards & modes
mscan2:
#endif /* CONFIG_VIDEO_SVGA */

mtabe:

#ifdef CONFIG_VIDEO_COMPACT
	leaw	modelist, %si
	movw	%di, %dx
	movw	%si, %di
cmt1:	cmpw	%dx, %si			# Scan all modes
	jz	cmt2

	leaw	modelist, %bx			# Find in previous entries
	movw	2(%si), %cx
cmt3:	cmpw	%bx, %si
	jz	cmt4

	cmpw	2(%bx), %cx			# Found => don't copy this entry
	jz	cmt5

	addw	$4, %bx
	jmp	cmt3

cmt4:	movsl					# Copy entry
	jmp	cmt1

cmt5:	addw	$4, %si				# Skip entry
	jmp	cmt1

cmt2:
#endif	/* CONFIG_VIDEO_COMPACT */

	movw	$ASK_VGA, (%di)			# End marker
	movw	%di, mt_end
mtab1:	leaw	modelist, %si			# SI=mode list, DI=list end
ret0:	ret

# Modes usable on all standard VGAs
vga_modes:
	.word	VIDEO_8POINT
	.word	0x5032				# 80x50
	.word	VIDEO_80x43
	.word	0x502b				# 80x43
	.word	VIDEO_80x28
	.word	0x501c				# 80x28
	.word	VIDEO_80x30
	.word	0x501e				# 80x30
	.word	VIDEO_80x34
	.word	0x5022				# 80x34
	.word	VIDEO_80x60
	.word	0x503c				# 80x60
#ifdef CONFIG_VIDEO_GFX_HACK
	.word	VIDEO_GFX_HACK
	.word	VIDEO_GFX_DUMMY_RESOLUTION
#endif

vga_modes_end:
# Detect VESA modes.

#ifdef CONFIG_VIDEO_VESA
vesa_modes:
	cmpb	$2, adapter			# VGA only
	jnz	ret0

	movw	%di, %bp			# BP=original mode table end
	addw	$0x200, %di			# Buffer space
	movw	$0x4f00, %ax			# VESA Get card info call
	int	$0x10
	movw	%bp, %di
	cmpw	$0x004f, %ax			# Successful?
	jnz	ret0
	
	cmpw	$0x4556, 0x200(%di)
	jnz	ret0
	
	cmpw	$0x4153, 0x202(%di)
	jnz	ret0
	
	movw	$vesa_name, card_name		# Set name to "VESA VGA"
	pushw	%gs
	lgsw	0x20e(%di), %si			# GS:SI=mode list
	movw	$128, %cx			# Iteration limit
vesa1:
# gas version 2.9.1, using BFD version 2.9.1.0.23 buggers the next inst.
# XXX:	lodsw	%gs:(%si), %ax			# Get next mode in the list
	gs; lodsw
	cmpw	$0xffff, %ax			# End of the table?
	jz	vesar
	
	cmpw	$0x0080, %ax			# Check validity of mode ID
	jc	vesa2
	
	orb	%ah, %ah		# Valid IDs: 0x0000-0x007f/0x0100-0x07ff
	jz	vesan			# Certain BIOSes report 0x80-0xff!

	cmpw	$0x0800, %ax
	jnc	vesae

vesa2:	pushw	%cx
	movw	%ax, %cx			# Get mode information structure
	movw	$0x4f01, %ax
	int	$0x10
	movw	%cx, %bx			# BX=mode number
	addb	$VIDEO_FIRST_VESA>>8, %bh
	popw	%cx
	cmpw	$0x004f, %ax
	jnz	vesan			# Don't report errors (buggy BIOSES)

	movb	(%di), %al			# Check capabilities. We require
	andb	$0x19, %al			# a color text mode.
	cmpb	$0x09, %al
	jnz	vesan
	
	cmpw	$0xb800, 8(%di)		# Standard video memory address required
	jnz	vesan

	testb	$2, (%di)			# Mode characteristics supplied?
	movw	%bx, (%di)			# Store mode number
	jz	vesa3
	
	xorw	%dx, %dx
	movw	0x12(%di), %bx			# Width
	orb	%bh, %bh
	jnz	vesan
	
	movb	%bl, 0x3(%di)
	movw	0x14(%di), %ax			# Height
	orb	%ah, %ah
	jnz	vesan
	
	movb	%al, 2(%di)
	mulb	%bl
	cmpw	$8193, %ax		# Small enough for Linux console driver?
	jnc	vesan

	jmp	vesaok

vesa3:	subw	$0x8108, %bx	# This mode has no detailed info specified,
	jc	vesan		# so it must be a standard VESA mode.

	cmpw	$5, %bx
	jnc	vesan

	movw	vesa_text_mode_table(%bx), %ax
	movw	%ax, 2(%di)
vesaok:	addw	$4, %di				# The mode is valid. Store it.
vesan:	loop	vesa1			# Next mode. Limit exceeded => error
vesae:	leaw	vesaer, %si
	call	prtstr
	movw	%bp, %di			# Discard already found modes.
vesar:	popw	%gs
	ret

# Dimensions of standard VESA text modes
vesa_text_mode_table:
	.byte	60, 80				# 0108
	.byte	25, 132				# 0109
	.byte	43, 132				# 010A
	.byte	50, 132				# 010B
	.byte	60, 132				# 010C
#endif	/* CONFIG_VIDEO_VESA */

# Scan for video modes. A bit dirty, but should work.
mode_scan:
	movw	$0x0100, %cx			# Start with mode 0
scm1:	movb	$0, %ah				# Test the mode
	movb	%cl, %al
	int	$0x10
	movb	$0x0f, %ah
	int	$0x10
	cmpb	%cl, %al
	jnz	scm2				# Mode not set

	movw	$0x3c0, %dx			# Test if it's a text mode
	movb	$0x10, %al			# Mode bits
	call	inidx
	andb	$0x03, %al
	jnz	scm2
	
	movb	$0xce, %dl			# Another set of mode bits
	movb	$0x06, %al
	call	inidx
	shrb	%al
	jc	scm2
	
	movb	$0xd4, %dl			# Cursor location
	movb	$0x0f, %al
	call	inidx
	orb	%al, %al
	jnz	scm2
	
	movw	%cx, %ax			# Ok, store the mode
	stosw
	movb	%gs:(0x484), %al		# Number of rows
	incb	%al
	stosb
	movw	%gs:(0x44a), %ax		# Number of columns
	stosb
scm2:	incb	%cl
	jns	scm1
	
	movw	$0x0003, %ax			# Return back to mode 3
	int	$0x10
	ret

tstidx:	outw	%ax, %dx			# OUT DX,AX and inidx
inidx:	outb	%al, %dx			# Read from indexed VGA register
	incw	%dx			# AL=index, DX=index reg port -> AL=data
	inb	%dx, %al
	decw	%dx
	ret

# Try to detect type of SVGA card and supply (usually approximate) video
# mode table for it.

#ifdef CONFIG_VIDEO_SVGA
svga_modes:
	leaw	svga_table, %si			# Test all known SVGA adapters
dosvga:	lodsw
	movw	%ax, %bp			# Default mode table
	orw	%ax, %ax
	jz	didsv1

	lodsw					# Pointer to test routine
	pushw	%si
	pushw	%di
	pushw	%es
	movw	$0xc000, %bx
	movw	%bx, %es
	call	*%ax				# Call test routine
	popw	%es
	popw	%di
	popw	%si
	orw	%bp, %bp
	jz	dosvga
	
	movw	%bp, %si			# Found, copy the modes
	movb	svga_prefix, %ah
cpsvga:	lodsb
	orb	%al, %al
	jz	didsv
	
	stosw
	movsw
	jmp	cpsvga

didsv:	movw	%si, card_name			# Store pointer to card name
didsv1:	ret

# Table of all known SVGA cards. For each card, we store a pointer to
# a table of video modes supported by the card and a pointer to a routine
# used for testing of presence of the card. The video mode table is always
# followed by the name of the card or the chipset.
svga_table:
	.word	ati_md, ati_test
	.word	oak_md, oak_test
	.word	paradise_md, paradise_test
	.word	realtek_md, realtek_test
	.word	s3_md, s3_test
	.word	chips_md, chips_test
	.word	video7_md, video7_test
	.word	cirrus5_md, cirrus5_test
	.word	cirrus6_md, cirrus6_test
	.word	cirrus1_md, cirrus1_test
	.word	ahead_md, ahead_test
	.word	everex_md, everex_test
	.word	genoa_md, genoa_test
	.word	trident_md, trident_test
	.word	tseng_md, tseng_test
	.word	0

# Test routines and mode tables:

# S3 - The test algorithm was taken from the SuperProbe package
# for XFree86 1.2.1. Report bugs to Christoph.Niemann@linux.org
s3_test:
	movw	$0x0f35, %cx	# we store some constants in cl/ch
	movw	$0x03d4, %dx
	movb	$0x38, %al
	call	inidx
	movb	%al, %bh	# store current CRT-register 0x38
	movw	$0x0038, %ax
	call	outidx		# disable writing to special regs
	movb	%cl, %al	# check whether we can write special reg 0x35
	call	inidx
	movb	%al, %bl	# save the current value of CRT reg 0x35
	andb	$0xf0, %al	# clear bits 0-3
	movb	%al, %ah
	movb	%cl, %al	# and write it to CRT reg 0x35
	call	outidx
	call	inidx		# now read it back
	andb	%ch, %al	# clear the upper 4 bits
	jz	s3_2		# the first test failed. But we have a

	movb	%bl, %ah	# second chance
	movb	%cl, %al
	call	outidx
	jmp	s3_1		# do the other tests

s3_2:	movw	%cx, %ax	# load ah with 0xf and al with 0x35
	orb	%bl, %ah	# set the upper 4 bits of ah with the orig value
	call	outidx		# write ...
	call	inidx		# ... and reread 
	andb	%cl, %al	# turn off the upper 4 bits
	pushw	%ax
	movb	%bl, %ah	# restore old value in register 0x35
	movb	%cl, %al
	call	outidx
	popw	%ax
	cmpb	%ch, %al	# setting lower 4 bits was successful => bad
	je	no_s3		# writing is allowed => this is not an S3

s3_1:	movw	$0x4838, %ax	# allow writing to special regs by putting
	call	outidx		# magic number into CRT-register 0x38
	movb	%cl, %al	# check whether we can write special reg 0x35
	call	inidx
	movb	%al, %bl
	andb	$0xf0, %al
	movb	%al, %ah
	movb	%cl, %al
	call	outidx
	call	inidx
	andb	%ch, %al
	jnz	no_s3		# no, we can't write => no S3

	movw	%cx, %ax
	orb	%bl, %ah
	call	outidx
	call	inidx
	andb	%ch, %al
	pushw	%ax
	movb	%bl, %ah	# restore old value in register 0x35
	movb	%cl, %al
	call	outidx
	popw	%ax
	cmpb	%ch, %al
	jne	no_s31		# writing not possible => no S3
	movb	$0x30, %al
	call	inidx		# now get the S3 id ...
	leaw	idS3, %di
	movw	$0x10, %cx
	repne
	scasb
	je	no_s31

	movb	%bh, %ah
	movb	$0x38, %al
	jmp	s3rest

no_s3:	movb	$0x35, %al	# restore CRT register 0x35
	movb	%bl, %ah
	call	outidx
no_s31:	xorw	%bp, %bp	# Detection failed
s3rest:	movb	%bh, %ah
	movb	$0x38, %al	# restore old value of CRT register 0x38
	jmp	outidx

idS3:	.byte	0x81, 0x82, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95
	.byte	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa8, 0xb0

s3_md:	.byte	0x54, 0x2b, 0x84
	.byte	0x55, 0x19, 0x84
	.byte	0
	.ascii	"S3"
	.byte	0

# ATI cards.
ati_test:
	leaw 	idati, %si
	movw	$0x31, %di
	movw	$0x09, %cx
	repe
	cmpsb
	je	atiok

	xorw	%bp, %bp
atiok:	ret

idati:	.ascii	"761295520"

ati_md:	.byte	0x23, 0x19, 0x84
	.byte	0x33, 0x2c, 0x84
	.byte	0x22, 0x1e, 0x64
	.byte	0x21, 0x19, 0x64
	.byte	0x58, 0x21, 0x50
	.byte	0x5b, 0x1e, 0x50
	.byte	0
	.ascii	"ATI"
	.byte	0

# AHEAD
ahead_test:
	movw	$0x200f, %ax
	movw	$0x3ce, %dx
	outw	%ax, %dx
	incw	%dx
	inb	%dx, %al
	cmpb	$0x20, %al
	je	isahed

	cmpb	$0x21, %al
	je	isahed
	
	xorw	%bp, %bp
isahed:	ret

ahead_md:
	.byte	0x22, 0x2c, 0x84
	.byte	0x23, 0x19, 0x84
	.byte	0x24, 0x1c, 0x84
	.byte	0x2f, 0x32, 0xa0
	.byte	0x32, 0x22, 0x50
	.byte	0x34, 0x42, 0x50
	.byte	0
	.ascii	"Ahead"
	.byte	0

# Chips & Tech.
chips_test:
	movw	$0x3c3, %dx
	inb	%dx, %al
	orb	$0x10, %al
	outb	%al, %dx
	movw	$0x104, %dx
	inb	%dx, %al
	movb	%al, %bl
	movw	$0x3c3, %dx
	inb	%dx, %al
	andb	$0xef, %al
	outb	%al, %dx
	cmpb	$0xa5, %bl
	je	cantok
	
	xorw	%bp, %bp
cantok:	ret

chips_md:
	.byte	0x60, 0x19, 0x84
	.byte	0x61, 0x32, 0x84
	.byte	0
	.ascii	"Chips & Technologies"
	.byte	0

# Cirrus Logic 5X0
cirrus1_test:
	movw	$0x3d4, %dx
	movb	$0x0c, %al
	outb	%al, %dx
	incw	%dx
	inb	%dx, %al
	movb	%al, %bl
	xorb	%al, %al
	outb	%al, %dx
	decw	%dx
	movb	$0x1f, %al
	outb	%al, %dx
	incw	%dx
	inb	%dx, %al
	movb	%al, %bh
	xorb	%ah, %ah
	shlb	$4, %al
	movw	%ax, %cx
	movb	%bh, %al
	shrb	$4, %al
	addw	%ax, %cx
	shlw	$8, %cx
	addw	$6, %cx
	movw	%cx, %ax
	movw	$0x3c4, %dx
	outw	%ax, %dx
	incw	%dx
	inb	%dx, %al
	andb	%al, %al
	jnz	nocirr
	
	movb	%bh, %al
	outb	%al, %dx
	inb	%dx, %al
	cmpb	$0x01, %al
	je	iscirr

nocirr:	xorw	%bp, %bp
iscirr: movw	$0x3d4, %dx
	movb	%bl, %al
	xorb	%ah, %ah
	shlw	$8, %ax
	addw	$0x0c, %ax
	outw	%ax, %dx
	ret

cirrus1_md:
	.byte	0x1f, 0x19, 0x84
	.byte	0x20, 0x2c, 0x84
	.byte	0x22, 0x1e, 0x84
	.byte	0x31, 0x25, 0x64
	.byte	0
	.ascii	"Cirrus Logic 5X0"
	.byte	0

# Cirrus Logic 54XX
cirrus5_test:
	movw	$0x3c4, %dx
	movb	$6, %al
	call	inidx
	movb	%al, %bl			# BL=backup
	movw	$6, %ax
	call	tstidx
	cmpb	$0x0f, %al
	jne	c5fail
	
	movw	$0x1206, %ax
	call	tstidx
	cmpb	$0x12, %al
	jne	c5fail
	
	movb	$0x1e, %al
	call	inidx
	movb	%al, %bh
	movb	%bh, %ah
	andb	$0xc0, %ah
	movb	$0x1e, %al
	call	tstidx
	andb	$0x3f, %al
	jne	c5xx
	
	movb	$0x1e, %al
	movb	%bh, %ah
	orb	$0x3f, %ah
	call	tstidx
	xorb	$0x3f, %al
	andb	$0x3f, %al
c5xx:	pushf
	movb	$0x1e, %al
	movb	%bh, %ah
	outw	%ax, %dx
	popf
	je	c5done

c5fail:	xorw	%bp, %bp
c5done:	movb	$6, %al
	movb	%bl, %ah
	outw	%ax, %dx
	ret

cirrus5_md:
	.byte	0x14, 0x19, 0x84
	.byte	0x54, 0x2b, 0x84
	.byte	0
	.ascii	"Cirrus Logic 54XX"
	.byte	0

# Cirrus Logic 64XX -- no known extra modes, but must be identified, because
# it's misidentified by the Ahead test.
cirrus6_test:
	movw	$0x3ce, %dx
	movb	$0x0a, %al
	call	inidx
	movb	%al, %bl	# BL=backup
	movw	$0xce0a, %ax
	call	tstidx
	orb	%al, %al
	jne	c2fail
	
	movw	$0xec0a, %ax
	call	tstidx
	cmpb	$0x01, %al
	jne	c2fail
	
	movb	$0xaa, %al
	call	inidx		# 4X, 5X, 7X and 8X are valid 64XX chip ID's. 
	shrb	$4, %al
	subb	$4, %al
	jz	c6done
	
	decb	%al
	jz	c6done
	
	subb	$2, %al
	jz	c6done
	
	decb	%al
	jz	c6done
	
c2fail:	xorw	%bp, %bp
c6done:	movb	$0x0a, %al
	movb	%bl, %ah
	outw	%ax, %dx
	ret

cirrus6_md:
	.byte	0
	.ascii	"Cirrus Logic 64XX"
	.byte	0

# Everex / Trident
everex_test:
	movw	$0x7000, %ax
	xorw	%bx, %bx
	int	$0x10
	cmpb	$0x70, %al
	jne	noevrx
	
	shrw	$4, %dx
	cmpw	$0x678, %dx
	je	evtrid
	
	cmpw	$0x236, %dx
	jne	evrxok

evtrid:	leaw	trident_md, %bp
evrxok:	ret

noevrx:	xorw	%bp, %bp
	ret

everex_md:
	.byte	0x03, 0x22, 0x50
	.byte	0x04, 0x3c, 0x50
	.byte	0x07, 0x2b, 0x64
	.byte	0x08, 0x4b, 0x64
	.byte	0x0a, 0x19, 0x84
	.byte	0x0b, 0x2c, 0x84
	.byte	0x16, 0x1e, 0x50
	.byte	0x18, 0x1b, 0x64
	.byte	0x21, 0x40, 0xa0
	.byte	0x40, 0x1e, 0x84
	.byte	0
	.ascii	"Everex/Trident"
	.byte	0

# Genoa.
genoa_test:
	leaw	idgenoa, %si			# Check Genoa 'clues'
	xorw	%ax, %ax
	movb	%es:(0x37), %al
	movw	%ax, %di
	movw	$0x04, %cx
	decw	%si
	decw	%di
l1:	incw	%si
	incw	%di
	movb	(%si), %al
	testb	%al, %al
	jz	l2

	cmpb	%es:(%di), %al
l2:	loope 	l1
	orw	%cx, %cx
	je	isgen
	
	xorw	%bp, %bp
isgen:	ret

idgenoa: .byte	0x77, 0x00, 0x99, 0x66

genoa_md:
	.byte	0x58, 0x20, 0x50
	.byte	0x5a, 0x2a, 0x64
	.byte	0x60, 0x19, 0x84
	.byte	0x61, 0x1d, 0x84
	.byte	0x62, 0x20, 0x84
	.byte	0x63, 0x2c, 0x84
	.byte	0x64, 0x3c, 0x84
	.byte	0x6b, 0x4f, 0x64
	.byte	0x72, 0x3c, 0x50
	.byte	0x74, 0x42, 0x50
	.byte	0x78, 0x4b, 0x64
	.byte	0
	.ascii	"Genoa"
	.byte	0

# OAK
oak_test:
	leaw	idoakvga, %si
	movw	$0x08, %di
	movw	$0x08, %cx
	repe
	cmpsb
	je	isoak
	
	xorw	%bp, %bp
isoak:	ret

idoakvga: .ascii  "OAK VGA "

oak_md: .byte	0x4e, 0x3c, 0x50
	.byte	0x4f, 0x3c, 0x84
	.byte	0x50, 0x19, 0x84
	.byte	0x51, 0x2b, 0x84
	.byte	0
	.ascii	"OAK"
	.byte	0

# WD Paradise.
paradise_test:
	leaw	idparadise, %si
	movw	$0x7d, %di
	movw	$0x04, %cx
	repe
	cmpsb
	je	ispara
	
	xorw	%bp, %bp
ispara:	ret

idparadise:	.ascii	"VGA="

paradise_md:
	.byte	0x41, 0x22, 0x50
	.byte	0x47, 0x1c, 0x84
	.byte	0x55, 0x19, 0x84
	.byte	0x54, 0x2c, 0x84
	.byte	0
	.ascii	"Paradise"
	.byte	0

# Trident.
trident_test:
	movw	$0x3c4, %dx
	movb	$0x0e, %al
	outb	%al, %dx
	incw	%dx
	inb	%dx, %al
	xchgb	%al, %ah
	xorb	%al, %al
	outb	%al, %dx
	inb	%dx, %al
	xchgb	%ah, %al
	movb	%al, %bl	# Strange thing ... in the book this wasn't
	andb	$0x02, %bl	# necessary but it worked on my card which
	jz	setb2		# is a trident. Without it the screen goes
				# blurred ...
	andb	$0xfd, %al
	jmp	clrb2		

setb2:	orb	$0x02, %al	
clrb2:	outb	%al, %dx
	andb	$0x0f, %ah
	cmpb	$0x02, %ah
	je	istrid

	xorw	%bp, %bp
istrid:	ret

trident_md:
	.byte	0x50, 0x1e, 0x50
	.byte	0x51, 0x2b, 0x50
	.byte	0x52, 0x3c, 0x50
	.byte	0x57, 0x19, 0x84
	.byte	0x58, 0x1e, 0x84
	.byte	0x59, 0x2b, 0x84
	.byte	0x5a, 0x3c, 0x84
	.byte	0
	.ascii	"Trident"
	.byte	0

# Tseng.
tseng_test:
	movw	$0x3cd, %dx
	inb	%dx, %al	# Could things be this simple ! :-)
	movb	%al, %bl
	movb	$0x55, %al
	outb	%al, %dx
	inb	%dx, %al
	movb	%al, %ah
	movb	%bl, %al
	outb	%al, %dx
	cmpb	$0x55, %ah
 	je	istsen

isnot:	xorw	%bp, %bp
istsen:	ret

tseng_md:
	.byte	0x26, 0x3c, 0x50
	.byte	0x2a, 0x28, 0x64
	.byte	0x23, 0x19, 0x84
	.byte	0x24, 0x1c, 0x84
	.byte	0x22, 0x2c, 0x84
	.byte	0x21, 0x3c, 0x84
	.byte	0
	.ascii	"Tseng"
	.byte	0

# Video7.
video7_test:
	movw	$0x3cc, %dx
	inb	%dx, %al
	movw	$0x3b4, %dx
	andb	$0x01, %al
	jz	even7

	movw	$0x3d4, %dx
even7:	movb	$0x0c, %al
	outb	%al, %dx
	incw	%dx
	inb	%dx, %al
	movb	%al, %bl
	movb	$0x55, %al
	outb	%al, %dx
	inb	%dx, %al
	decw	%dx
	movb	$0x1f, %al
	outb	%al, %dx
	incw	%dx
	inb	%dx, %al
	movb	%al, %bh
	decw	%dx
	movb	$0x0c, %al
	outb	%al, %dx
	incw	%dx
	movb	%bl, %al
	outb	%al, %dx
	movb	$0x55, %al
	xorb	$0xea, %al
	cmpb	%bh, %al
	jne	isnot
	
	movb	$VIDEO_FIRST_V7>>8, svga_prefix # Use special mode switching
	ret

video7_md:
	.byte	0x40, 0x2b, 0x50
	.byte	0x43, 0x3c, 0x50
	.byte	0x44, 0x3c, 0x64
	.byte	0x41, 0x19, 0x84
	.byte	0x42, 0x2c, 0x84
	.byte	0x45, 0x1c, 0x84
	.byte	0
	.ascii	"Video 7"
	.byte	0

# Realtek VGA
realtek_test:
	leaw	idrtvga, %si
	movw	$0x45, %di
	movw	$0x0b, %cx
	repe
	cmpsb
	je	isrt
	
	xorw	%bp, %bp
isrt:	ret

idrtvga:	.ascii	"REALTEK VGA"

realtek_md:
	.byte	0x1a, 0x3c, 0x50
	.byte	0x1b, 0x19, 0x84
	.byte	0x1c, 0x1e, 0x84
	.byte	0x1d, 0x2b, 0x84
	.byte	0x1e, 0x3c, 0x84
	.byte	0
	.ascii	"REALTEK"
	.byte	0

#endif	/* CONFIG_VIDEO_SVGA */

# User-defined local mode table (VGA only)
#ifdef CONFIG_VIDEO_LOCAL
local_modes:
	leaw	local_mode_table, %si
locm1:	lodsw
	orw	%ax, %ax
	jz	locm2
	
	stosw
	movsw
	jmp	locm1

locm2:	ret

# This is the table of local video modes which can be supplied manually
# by the user. Each entry consists of mode ID (word) and dimensions
# (byte for column count and another byte for row count). These modes
# are placed before all SVGA and VESA modes and override them if table
# compacting is enabled. The table must end with a zero word followed
# by NUL-terminated video adapter name.
local_mode_table:
	.word	0x0100				# Example: 40x25
	.byte	25,40
	.word	0
	.ascii	"Local"
	.byte	0
#endif	/* CONFIG_VIDEO_LOCAL */

# Read a key and return the ASCII code in al, scan code in ah
getkey:	xorb	%ah, %ah
	int	$0x16
	ret

# Read a key with a timeout of 30 seconds.
# The hardware clock is used to get the time.
getkt:	call	gettime
	addb	$30, %al			# Wait 30 seconds
	cmpb	$60, %al
	jl	lminute

	subb	$60, %al
lminute:
	movb	%al, %cl
again:	movb	$0x01, %ah
	int	$0x16
	jnz	getkey				# key pressed, so get it

	call	gettime
	cmpb	%cl, %al
	jne	again

	movb	$0x20, %al			# timeout, return `space'
	ret

# Flush the keyboard buffer
flush:	movb	$0x01, %ah
	int	$0x16
	jz	empty
	
	xorb	%ah, %ah
	int	$0x16
	jmp	flush

empty:	ret

# Print hexadecimal number.
prthw:	pushw	%ax
	movb	%ah, %al
	call	prthb
	popw	%ax
prthb:	pushw	%ax
	shrb	$4, %al
	call	prthn
	popw	%ax
	andb	$0x0f, %al
prthn:	cmpb	$0x0a, %al
	jc	prth1

	addb	$0x07, %al
prth1:	addb	$0x30, %al
	jmp	prtchr

# Print decimal number in al
prtdec:	pushw	%ax
	pushw	%cx
	xorb	%ah, %ah
	movb	$0x0a, %cl
	idivb	%cl
	cmpb	$0x09, %al
	jbe	lt100

	call	prtdec
	jmp	skip10

lt100:	addb	$0x30, %al
	call	prtchr
skip10:	movb	%ah, %al
	addb	$0x30, %al
	call	prtchr	
	popw	%cx
	popw	%ax
	ret

store_edid:
#ifdef CONFIG_FIRMWARE_EDID
	pushw	%es				# just save all registers
	pushw	%ax
	pushw	%bx
	pushw   %cx
	pushw	%dx
	pushw   %di

	pushw	%fs
	popw    %es

	movl	$0x13131313, %eax		# memset block with 0x13
	movw    $32, %cx
	movw	$0x140, %di
	cld
	rep
	stosl

	cmpw	$0x0200, vbe_version		# only do EDID on >= VBE2.0
	jl	no_edid

	pushw   %es				# save ES
	xorw    %di, %di                        # Report Capability
	pushw   %di
	popw    %es                             # ES:DI must be 0:0
	movw	$0x4f15, %ax
	xorw	%bx, %bx
	xorw	%cx, %cx
	int	$0x10
	popw    %es                             # restore ES

	cmpb    $0x00, %ah                      # call successful
	jne     no_edid

	cmpb    $0x4f, %al                      # function supported
	jne     no_edid

	movw	$0x4f15, %ax                    # do VBE/DDC
	movw	$0x01, %bx
	movw	$0x00, %cx
	movw    $0x01, %dx
	movw	$0x140, %di
	int	$0x10

no_edid:
	popw	%di				# restore all registers
	popw	%dx
	popw	%cx
	popw	%bx
	popw	%ax
	popw	%es
#endif
	ret

# VIDEO_SELECT-only variables
mt_end:		.word	0	# End of video mode table if built
edit_buf:	.space	6	# Line editor buffer
card_name:	.word	0	# Pointer to adapter name
scanning:	.byte	0	# Performing mode scan
do_restore:	.byte	0	# Screen contents altered during mode change
svga_prefix:	.byte	VIDEO_FIRST_BIOS>>8	# Default prefix for BIOS modes
graphic_mode:	.byte	0	# Graphic mode with a linear frame buffer
dac_size:	.byte	6	# DAC bit depth
vbe_version:	.word	0	# VBE bios version

# Status messages
keymsg:		.ascii	"Press <RETURN> to see video modes available, "
		.ascii	"<SPACE> to continue or wait 30 secs"
		.byte	0x0d, 0x0a, 0

listhdr:	.byte	0x0d, 0x0a
		.ascii	"Mode:    COLSxROWS:"

crlft:		.byte	0x0d, 0x0a, 0

prompt:		.byte	0x0d, 0x0a
		.asciz	"Enter mode number or `scan': "

unknt:		.asciz	"Unknown mode ID. Try again."

badmdt:		.ascii	"You passed an undefined mode number."
		.byte	0x0d, 0x0a, 0

vesaer:		.ascii	"Error: Scanning of VESA modes failed. Please "
		.ascii	"report to <mj@ucw.cz>."
		.byte	0x0d, 0x0a, 0

old_name:	.asciz	"CGA/MDA/HGA"

ega_name:	.asciz	"EGA"

svga_name:	.ascii	" "

vga_name:	.asciz	"VGA"

vesa_name:	.asciz	"VESA"

name_bann:	.asciz	"Video adapter: "
#endif /* CONFIG_VIDEO_SELECT */

# Other variables:
adapter:	.byte	0	# Video adapter: 0=CGA/MDA/HGA,1=EGA,2=VGA
video_segment:	.word	0xb800	# Video memory segment
force_size:	.word	0	# Use this size instead of the one in BIOS vars
