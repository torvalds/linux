#! /usr/bin/env perl
# Copyright 2004-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# Eternal question is what's wrong with compiler generated code? The
# trick is that it's possible to reduce the number of shifts required
# to perform rotations by maintaining copy of 32-bit value in upper
# bits of 64-bit register. Just follow mux2 and shrp instructions...
# Performance under big-endian OS such as HP-UX is 179MBps*1GHz, which
# is >50% better than HP C and >2x better than gcc.

$output = pop;

$code=<<___;
.ident  \"sha1-ia64.s, version 1.3\"
.ident  \"IA-64 ISA artwork by Andy Polyakov <appro\@fy.chalmers.se>\"
.explicit

___


if ($^O eq "hpux") {
    $ADDP="addp4";
    for (@ARGV) { $ADDP="add" if (/[\+DD|\-mlp]64/); }
} else { $ADDP="add"; }

#$human=1;
if ($human) {	# useful for visual code auditing...
	($A,$B,$C,$D,$E)   = ("A","B","C","D","E");
	($h0,$h1,$h2,$h3,$h4) = ("h0","h1","h2","h3","h4");
	($K_00_19, $K_20_39, $K_40_59, $K_60_79) =
	    (	"K_00_19","K_20_39","K_40_59","K_60_79"	);
	@X= (	"X0", "X1", "X2", "X3", "X4", "X5", "X6", "X7",
		"X8", "X9","X10","X11","X12","X13","X14","X15"	);
}
else {
	($A,$B,$C,$D,$E)   =    ("loc0","loc1","loc2","loc3","loc4");
	($h0,$h1,$h2,$h3,$h4) = ("loc5","loc6","loc7","loc8","loc9");
	($K_00_19, $K_20_39, $K_40_59, $K_60_79) =
	    (	"r14", "r15", "loc10", "loc11"	);
	@X= (	"r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
		"r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31"	);
}

sub BODY_00_15 {
local	*code=shift;
my	($i,$a,$b,$c,$d,$e)=@_;
my	$j=$i+1;
my	$Xn=@X[$j%16];

$code.=<<___ if ($i==0);
{ .mmi;	ld1	$X[$i]=[inp],2		    // MSB
	ld1	tmp2=[tmp3],2		};;
{ .mmi;	ld1	tmp0=[inp],2
	ld1	tmp4=[tmp3],2		    // LSB
	dep	$X[$i]=$X[$i],tmp2,8,8	};;
___
if ($i<15) {
	$code.=<<___;
{ .mmi;	ld1	$Xn=[inp],2		    // forward Xload
	nop.m	0x0
	dep	tmp1=tmp0,tmp4,8,8	};;
{ .mmi;	ld1	tmp2=[tmp3],2		    // forward Xload
	and	tmp4=$c,$b
	dep	$X[$i]=$X[$i],tmp1,16,16} //;;
{ .mmi;	add	$e=$e,$K_00_19		    // e+=K_00_19
	andcm	tmp1=$d,$b
	dep.z	tmp5=$a,5,27		};; // a<<5
{ .mmi;	add	$e=$e,$X[$i]		    // e+=Xload
	or	tmp4=tmp4,tmp1		    // F_00_19(b,c,d)=(b&c)|(~b&d)
	extr.u	tmp1=$a,27,5		};; // a>>27
{ .mmi;	ld1	tmp0=[inp],2		    // forward Xload
	add	$e=$e,tmp4		    // e+=F_00_19(b,c,d)
	shrp	$b=tmp6,tmp6,2		}   // b=ROTATE(b,30)
{ .mmi;	ld1	tmp4=[tmp3],2		    // forward Xload
	or	tmp5=tmp1,tmp5		    // ROTATE(a,5)
	mux2	tmp6=$a,0x44		};; // see b in next iteration
{ .mii;	add	$e=$e,tmp5		    // e+=ROTATE(a,5)
	dep	$Xn=$Xn,tmp2,8,8	    // forward Xload
	mux2	$X[$i]=$X[$i],0x44	} //;;

___
	}
else	{
	$code.=<<___;
{ .mii;	and	tmp3=$c,$b
	dep	tmp1=tmp0,tmp4,8,8;;
	dep	$X[$i]=$X[$i],tmp1,16,16} //;;
{ .mmi;	add	$e=$e,$K_00_19		    // e+=K_00_19
	andcm	tmp1=$d,$b
	dep.z	tmp5=$a,5,27		};; // a<<5
{ .mmi;	add	$e=$e,$X[$i]		    // e+=Xupdate
	or	tmp4=tmp3,tmp1		    // F_00_19(b,c,d)=(b&c)|(~b&d)
	extr.u	tmp1=$a,27,5		}   // a>>27
{ .mmi;	xor	$Xn=$Xn,$X[($j+2)%16]	    // forward Xupdate
	xor	tmp3=$X[($j+8)%16],$X[($j+13)%16] // forward Xupdate
	nop.i	0			};;
{ .mmi;	add	$e=$e,tmp4		    // e+=F_00_19(b,c,d)
	xor	$Xn=$Xn,tmp3		    // forward Xupdate
	shrp	$b=tmp6,tmp6,2		}   // b=ROTATE(b,30)
{ .mmi; or	tmp1=tmp1,tmp5		    // ROTATE(a,5)
	mux2	tmp6=$a,0x44		};; // see b in next iteration
{ .mii;	add	$e=$e,tmp1		    // e+=ROTATE(a,5)
	shrp	$Xn=$Xn,$Xn,31		    // ROTATE(x[0]^x[2]^x[8]^x[13],1)
	mux2	$X[$i]=$X[$i],0x44	};;

___
	}
}

sub BODY_16_19 {
local	*code=shift;
my	($i,$a,$b,$c,$d,$e)=@_;
my	$j=$i+1;
my	$Xn=@X[$j%16];

$code.=<<___;
{ .mib;	add	$e=$e,$K_00_19		    // e+=K_00_19
	dep.z	tmp5=$a,5,27		}   // a<<5
{ .mib;	andcm	tmp1=$d,$b
	and	tmp0=$c,$b		};;
{ .mmi;	add	$e=$e,$X[$i%16]		    // e+=Xupdate
	or	tmp0=tmp0,tmp1		    // F_00_19(b,c,d)=(b&c)|(~b&d)
	extr.u	tmp1=$a,27,5		}   // a>>27
{ .mmi;	xor	$Xn=$Xn,$X[($j+2)%16]	    // forward Xupdate
	xor	tmp3=$X[($j+8)%16],$X[($j+13)%16]	// forward Xupdate
	nop.i	0			};;
{ .mmi;	add	$e=$e,tmp0		    // f+=F_00_19(b,c,d)
	xor	$Xn=$Xn,tmp3		    // forward Xupdate
	shrp	$b=tmp6,tmp6,2		}   // b=ROTATE(b,30)
{ .mmi;	or	tmp1=tmp1,tmp5		    // ROTATE(a,5)
	mux2	tmp6=$a,0x44		};; // see b in next iteration
{ .mii;	add	$e=$e,tmp1		    // e+=ROTATE(a,5)
	shrp	$Xn=$Xn,$Xn,31		    // ROTATE(x[0]^x[2]^x[8]^x[13],1)
	nop.i	0			};;

___
}

sub BODY_20_39 {
local	*code=shift;
my	($i,$a,$b,$c,$d,$e,$Konst)=@_;
	$Konst = $K_20_39 if (!defined($Konst));
my	$j=$i+1;
my	$Xn=@X[$j%16];

if ($i<79) {
$code.=<<___;
{ .mib;	add	$e=$e,$Konst		    // e+=K_XX_XX
	dep.z	tmp5=$a,5,27		}   // a<<5
{ .mib;	xor	tmp0=$c,$b
	xor	$Xn=$Xn,$X[($j+2)%16]	};; // forward Xupdate
{ .mib;	add	$e=$e,$X[$i%16]		    // e+=Xupdate
	extr.u	tmp1=$a,27,5		}   // a>>27
{ .mib;	xor	tmp0=tmp0,$d		    // F_20_39(b,c,d)=b^c^d
	xor	$Xn=$Xn,$X[($j+8)%16]	};; // forward Xupdate
{ .mmi;	add	$e=$e,tmp0		    // e+=F_20_39(b,c,d)
	xor	$Xn=$Xn,$X[($j+13)%16]	    // forward Xupdate
	shrp	$b=tmp6,tmp6,2		}   // b=ROTATE(b,30)
{ .mmi;	or	tmp1=tmp1,tmp5		    // ROTATE(a,5)
	mux2	tmp6=$a,0x44		};; // see b in next iteration
{ .mii;	add	$e=$e,tmp1		    // e+=ROTATE(a,5)
	shrp	$Xn=$Xn,$Xn,31		    // ROTATE(x[0]^x[2]^x[8]^x[13],1)
	nop.i	0			};;

___
}
else {
$code.=<<___;
{ .mib;	add	$e=$e,$Konst		    // e+=K_60_79
	dep.z	tmp5=$a,5,27		}   // a<<5
{ .mib;	xor	tmp0=$c,$b
	add	$h1=$h1,$a		};; // wrap up
{ .mib;	add	$e=$e,$X[$i%16]		    // e+=Xupdate
	extr.u	tmp1=$a,27,5		}   // a>>27
{ .mib;	xor	tmp0=tmp0,$d		    // F_20_39(b,c,d)=b^c^d
	add	$h3=$h3,$c		};; // wrap up
{ .mmi;	add	$e=$e,tmp0		    // e+=F_20_39(b,c,d)
	or	tmp1=tmp1,tmp5		    // ROTATE(a,5)
	shrp	$b=tmp6,tmp6,2		};; // b=ROTATE(b,30) ;;?
{ .mmi;	add	$e=$e,tmp1		    // e+=ROTATE(a,5)
	add	tmp3=1,inp		    // used in unaligned codepath
	add	$h4=$h4,$d		};; // wrap up

___
}
}

sub BODY_40_59 {
local	*code=shift;
my	($i,$a,$b,$c,$d,$e)=@_;
my	$j=$i+1;
my	$Xn=@X[$j%16];

$code.=<<___;
{ .mib;	add	$e=$e,$K_40_59		    // e+=K_40_59
	dep.z	tmp5=$a,5,27		}   // a<<5
{ .mib;	and	tmp1=$c,$d
	xor	tmp0=$c,$d		};;
{ .mmi;	add	$e=$e,$X[$i%16]		    // e+=Xupdate
	add	tmp5=tmp5,tmp1		    // a<<5+(c&d)
	extr.u	tmp1=$a,27,5		}   // a>>27
{ .mmi;	and	tmp0=tmp0,$b
	xor	$Xn=$Xn,$X[($j+2)%16]	    // forward Xupdate
	xor	tmp3=$X[($j+8)%16],$X[($j+13)%16] };;	// forward Xupdate
{ .mmi;	add	$e=$e,tmp0		    // e+=b&(c^d)
	add	tmp5=tmp5,tmp1		    // ROTATE(a,5)+(c&d)
	shrp	$b=tmp6,tmp6,2		}   // b=ROTATE(b,30)
{ .mmi;	xor	$Xn=$Xn,tmp3
	mux2	tmp6=$a,0x44		};; // see b in next iteration
{ .mii;	add	$e=$e,tmp5		    // e+=ROTATE(a,5)+(c&d)
	shrp	$Xn=$Xn,$Xn,31		    // ROTATE(x[0]^x[2]^x[8]^x[13],1)
	nop.i	0x0			};;

___
}
sub BODY_60_79	{ &BODY_20_39(@_,$K_60_79); }

$code.=<<___;
.text

tmp0=r8;
tmp1=r9;
tmp2=r10;
tmp3=r11;
ctx=r32;	// in0
inp=r33;	// in1

// void sha1_block_data_order(SHA_CTX *c,const void *p,size_t num);
.global	sha1_block_data_order#
.proc	sha1_block_data_order#
.align	32
sha1_block_data_order:
	.prologue
{ .mmi;	alloc	tmp1=ar.pfs,3,14,0,0
	$ADDP	tmp0=4,ctx
	.save	ar.lc,r3
	mov	r3=ar.lc		}
{ .mmi;	$ADDP	ctx=0,ctx
	$ADDP	inp=0,inp
	mov	r2=pr			};;
tmp4=in2;
tmp5=loc12;
tmp6=loc13;
	.body
{ .mlx;	ld4	$h0=[ctx],8
	movl	$K_00_19=0x5a827999	}
{ .mlx;	ld4	$h1=[tmp0],8
	movl	$K_20_39=0x6ed9eba1	};;
{ .mlx;	ld4	$h2=[ctx],8
	movl	$K_40_59=0x8f1bbcdc	}
{ .mlx;	ld4	$h3=[tmp0]
	movl	$K_60_79=0xca62c1d6	};;
{ .mmi;	ld4	$h4=[ctx],-16
	add	in2=-1,in2		    // adjust num for ar.lc
	mov	ar.ec=1			};;
{ .mmi;	nop.m	0
	add	tmp3=1,inp
	mov	ar.lc=in2		};; // brp.loop.imp: too far

.Ldtop:
{ .mmi;	mov	$A=$h0
	mov	$B=$h1
	mux2	tmp6=$h1,0x44		}
{ .mmi;	mov	$C=$h2
	mov	$D=$h3
	mov	$E=$h4			};;

___

{ my $i;
  my @V=($A,$B,$C,$D,$E);

	for($i=0;$i<16;$i++)	{ &BODY_00_15(\$code,$i,@V); unshift(@V,pop(@V)); }
	for(;$i<20;$i++)	{ &BODY_16_19(\$code,$i,@V); unshift(@V,pop(@V)); }
	for(;$i<40;$i++)	{ &BODY_20_39(\$code,$i,@V); unshift(@V,pop(@V)); }
	for(;$i<60;$i++)	{ &BODY_40_59(\$code,$i,@V); unshift(@V,pop(@V)); }
	for(;$i<80;$i++)	{ &BODY_60_79(\$code,$i,@V); unshift(@V,pop(@V)); }

	(($V[0] eq $A) and ($V[4] eq $E)) or die;	# double-check
}

$code.=<<___;
{ .mmb;	add	$h0=$h0,$A
	add	$h2=$h2,$C
	br.ctop.dptk.many	.Ldtop	};;
.Ldend:
{ .mmi;	add	tmp0=4,ctx
	mov	ar.lc=r3		};;
{ .mmi;	st4	[ctx]=$h0,8
	st4	[tmp0]=$h1,8		};;
{ .mmi;	st4	[ctx]=$h2,8
	st4	[tmp0]=$h3		};;
{ .mib;	st4	[ctx]=$h4,-16
	mov	pr=r2,0x1ffff
	br.ret.sptk.many	b0	};;
.endp	sha1_block_data_order#
stringz	"SHA1 block transform for IA64, CRYPTOGAMS by <appro\@openssl.org>"
___

open STDOUT,">$output" if $output;
print $code;
