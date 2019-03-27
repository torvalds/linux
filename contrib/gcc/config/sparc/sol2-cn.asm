! crtn.s for solaris 2.0.

!   Copyright (C) 1992 Free Software Foundation, Inc.
!   Written By David Vinayak Henkel-Wallace, June 1992
! 
! This file is free software; you can redistribute it and/or modify it
! under the terms of the GNU General Public License as published by the
! Free Software Foundation; either version 2, or (at your option) any
! later version.
! 
! In addition to the permissions in the GNU General Public License, the
! Free Software Foundation gives you unlimited permission to link the
! compiled version of this file with other programs, and to distribute
! those programs without any restriction coming from the use of this
! file.  (The General Public License restrictions do apply in other
! respects; for example, they cover modification of the file, and
! distribution when not linked into another program.)
! 
! This file is distributed in the hope that it will be useful, but
! WITHOUT ANY WARRANTY; without even the implied warranty of
! MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
! General Public License for more details.
! 
! You should have received a copy of the GNU General Public License
! along with this program; see the file COPYING.  If not, write to
! the Free Software Foundation, 51 Franklin Street, Fifth Floor,
! Boston, MA 02110-1301, USA.
! 
!    As a special exception, if you link this library with files
!    compiled with GCC to produce an executable, this does not cause
!    the resulting executable to be covered by the GNU General Public License.
!    This exception does not however invalidate any other reasons why
!    the executable file might be covered by the GNU General Public License.
! 

! This file just makes sure that the .fini and .init sections do in
! fact return.  Users may put any desired instructions in those sections.
! This file is the last thing linked into any executable.

	.file		"crtn.s"

	.section	".init"
	.align		4

	ret
	restore

	.section	".fini"
	.align		4

	ret
	restore

! Th-th-th-that is all folks!
