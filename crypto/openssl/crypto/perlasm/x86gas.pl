#! /usr/bin/env perl
# Copyright 2007-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


package x86gas;

*out=\@::out;

$::lbdecor=$::aout?"L":".L";		# local label decoration
$nmdecor=($::aout or $::coff)?"_":"";	# external name decoration

$initseg="";

$align=16;
$align=log($align)/log(2) if ($::aout);
$com_start="#" if ($::aout or $::coff);

sub opsize()
{ my $reg=shift;
    if    ($reg =~ m/^%e/o)		{ "l"; }
    elsif ($reg =~ m/^%[a-d][hl]$/o)	{ "b"; }
    elsif ($reg =~ m/^%[yxm]/o)		{ undef; }
    else				{ "w"; }
}

# swap arguments;
# expand opcode with size suffix;
# prefix numeric constants with $;
sub ::generic
{ my($opcode,@arg)=@_;
  my($suffix,$dst,$src);

    @arg=reverse(@arg);

    for (@arg)
    {	s/^(\*?)(e?[a-dsixphl]{2})$/$1%$2/o;	# gp registers
	s/^([xy]?mm[0-7])$/%$1/o;		# xmm/mmx registers
	s/^(\-?[0-9]+)$/\$$1/o;			# constants
	s/^(\-?0x[0-9a-f]+)$/\$$1/o;		# constants
    }

    $dst = $arg[$#arg]		if ($#arg>=0);
    $src = $arg[$#arg-1]	if ($#arg>=1);
    if    ($dst =~ m/^%/o)	{ $suffix=&opsize($dst); }
    elsif ($src =~ m/^%/o)	{ $suffix=&opsize($src); }
    else			{ $suffix="l";           }
    undef $suffix if ($dst =~ m/^%[xm]/o || $src =~ m/^%[xm]/o);

    if ($#_==0)				{ &::emit($opcode);		}
    elsif ($#_==1 && $opcode =~ m/^(call|clflush|j|loop|set)/o)
					{ &::emit($opcode,@arg);	}
    else				{ &::emit($opcode.$suffix,@arg);}

  1;
}
#
# opcodes not covered by ::generic above, mostly inconsistent namings...
#
sub ::movzx	{ &::movzb(@_);			}
sub ::pushfd	{ &::pushfl;			}
sub ::popfd	{ &::popfl;			}
sub ::cpuid	{ &::emit(".byte\t0x0f,0xa2");	}
sub ::rdtsc	{ &::emit(".byte\t0x0f,0x31");	}

sub ::call	{ &::emit("call",(&::islabel($_[0]) or "$nmdecor$_[0]")); }
sub ::call_ptr	{ &::generic("call","*$_[0]");	}
sub ::jmp_ptr	{ &::generic("jmp","*$_[0]");	}

*::bswap = sub	{ &::emit("bswap","%$_[0]");	} if (!$::i386);

sub ::DWP
{ my($addr,$reg1,$reg2,$idx)=@_;
  my $ret="";

    if (!defined($idx) && 1*$reg2) { $idx=$reg2; $reg2=$reg1; undef $reg1; }

    $addr =~ s/^\s+//;
    # prepend global references with optional underscore
    $addr =~ s/^([^\+\-0-9][^\+\-]*)/&::islabel($1) or "$nmdecor$1"/ige;

    $reg1 = "%$reg1" if ($reg1);
    $reg2 = "%$reg2" if ($reg2);

    $ret .= $addr if (($addr ne "") && ($addr ne 0));

    if ($reg2)
    {	$idx!= 0 or $idx=1;
	$ret .= "($reg1,$reg2,$idx)";
    }
    elsif ($reg1)
    {	$ret .= "($reg1)";	}

  $ret;
}
sub ::QWP	{ &::DWP(@_);	}
sub ::BP	{ &::DWP(@_);	}
sub ::WP	{ &::DWP(@_);	}
sub ::BC	{ @_;		}
sub ::DWC	{ @_;		}

sub ::file
{   push(@out,".text\n");	}

sub ::function_begin_B
{ my $func=shift;
  my $global=($func !~ /^_/);
  my $begin="${::lbdecor}_${func}_begin";

    &::LABEL($func,$global?"$begin":"$nmdecor$func");
    $func=$nmdecor.$func;

    push(@out,".globl\t$func\n")	if ($global);
    if ($::coff)
    {	push(@out,".def\t$func;\t.scl\t".(3-$global).";\t.type\t32;\t.endef\n"); }
    elsif (($::aout and !$::pic) or $::macosx)
    { }
    else
    {	push(@out,".type	$func,\@function\n"); }
    push(@out,".align\t$align\n");
    push(@out,"$func:\n");
    push(@out,"$begin:\n")		if ($global);
    $::stack=4;
}

sub ::function_end_B
{ my $func=shift;
    push(@out,".size\t$nmdecor$func,.-".&::LABEL($func)."\n") if ($::elf);
    $::stack=0;
    &::wipe_labels();
}

sub ::comment
	{
	if (!defined($com_start) or $::elf)
		{	# Regarding $::elf above...
			# GNU and SVR4 as'es use different comment delimiters,
		push(@out,"\n");	# so we just skip ELF comments...
		return;
		}
	foreach (@_)
		{
		if (/^\s*$/)
			{ push(@out,"\n"); }
		else
			{ push(@out,"\t$com_start $_ $com_end\n"); }
		}
	}

sub ::external_label
{   foreach(@_) { &::LABEL($_,$nmdecor.$_); }   }

sub ::public_label
{   push(@out,".globl\t".&::LABEL($_[0],$nmdecor.$_[0])."\n");   }

sub ::file_end
{   if ($::macosx)
    {	if (%non_lazy_ptr)
    	{   push(@out,".section __IMPORT,__pointers,non_lazy_symbol_pointers\n");
	    foreach $i (keys %non_lazy_ptr)
	    {	push(@out,"$non_lazy_ptr{$i}:\n.indirect_symbol\t$i\n.long\t0\n");   }
	}
    }
    if (grep {/\b${nmdecor}OPENSSL_ia32cap_P\b/i} @out) {
	my $tmp=".comm\t${nmdecor}OPENSSL_ia32cap_P,16";
	if ($::macosx)	{ push (@out,"$tmp,2\n"); }
	elsif ($::elf)	{ push (@out,"$tmp,4\n"); }
	else		{ push (@out,"$tmp\n"); }
    }
    push(@out,$initseg) if ($initseg);
}

sub ::data_byte	{   push(@out,".byte\t".join(',',@_)."\n");   }
sub ::data_short{   push(@out,".value\t".join(',',@_)."\n");  }
sub ::data_word {   push(@out,".long\t".join(',',@_)."\n");   }

sub ::align
{ my $val=$_[0];
    if ($::aout)
    {	$val=int(log($val)/log(2));
	$val.=",0x90";
    }
    push(@out,".align\t$val\n");
}

sub ::picmeup
{ my($dst,$sym,$base,$reflabel)=@_;

    if (($::pic && ($::elf || $::aout)) || $::macosx)
    {	if (!defined($base))
	{   &::call(&::label("PIC_me_up"));
	    &::set_label("PIC_me_up");
	    &::blindpop($dst);
	    $base=$dst;
	    $reflabel=&::label("PIC_me_up");
	}
	if ($::macosx)
	{   my $indirect=&::static_label("$nmdecor$sym\$non_lazy_ptr");
	    &::mov($dst,&::DWP("$indirect-$reflabel",$base));
	    $non_lazy_ptr{"$nmdecor$sym"}=$indirect;
	}
	elsif ($sym eq "OPENSSL_ia32cap_P" && $::elf>0)
	{   &::lea($dst,&::DWP("$sym-$reflabel",$base));   }
	else
	{   &::lea($dst,&::DWP("_GLOBAL_OFFSET_TABLE_+[.-$reflabel]",
			    $base));
	    &::mov($dst,&::DWP("$sym\@GOT",$dst));
	}
    }
    else
    {	&::lea($dst,&::DWP($sym));	}
}

sub ::initseg
{ my $f=$nmdecor.shift;

    if ($::android)
    {	$initseg.=<<___;
.section	.init_array
.align	4
.long	$f
___
    }
    elsif ($::elf)
    {	$initseg.=<<___;
.section	.init
	call	$f
___
    }
    elsif ($::coff)
    {   $initseg.=<<___;	# applies to both Cygwin and Mingw
.section	.ctors
.long	$f
___
    }
    elsif ($::macosx)
    {	$initseg.=<<___;
.mod_init_func
.align 2
.long   $f
___
    }
    elsif ($::aout)
    {	my $ctor="${nmdecor}_GLOBAL_\$I\$$f";
	$initseg.=".text\n";
	$initseg.=".type	$ctor,\@function\n" if ($::pic);
	$initseg.=<<___;	# OpenBSD way...
.globl	$ctor
.align	2
$ctor:
	jmp	$f
___
    }
}

sub ::dataseg
{   push(@out,".data\n");   }

*::hidden = sub { push(@out,".hidden\t$nmdecor$_[0]\n"); } if ($::elf);

1;
