#! /usr/bin/env perl
# Copyright 2011-2018 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# September 2011
#
# Assembler helpers for Padlock engine. See even e_padlock-x86.pl for
# details.

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../crypto/perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

$code=".text\n";

%PADLOCK_PREFETCH=(ecb=>128, cbc=>64, ctr32=>32);	# prefetch errata
$PADLOCK_CHUNK=512;	# Must be a power of 2 between 32 and 2^20

$ctx="%rdx";
$out="%rdi";
$inp="%rsi";
$len="%rcx";
$chunk="%rbx";

($arg1,$arg2,$arg3,$arg4)=$win64?("%rcx","%rdx","%r8", "%r9") : # Win64 order
                                 ("%rdi","%rsi","%rdx","%rcx"); # Unix order

$code.=<<___;
.globl	padlock_capability
.type	padlock_capability,\@abi-omnipotent
.align	16
padlock_capability:
	mov	%rbx,%r8
	xor	%eax,%eax
	cpuid
	xor	%eax,%eax
	cmp	\$`"0x".unpack("H*",'tneC')`,%ebx
	jne	.Lzhaoxin
	cmp	\$`"0x".unpack("H*",'Hrua')`,%edx
	jne	.Lnoluck
	cmp	\$`"0x".unpack("H*",'slua')`,%ecx
	jne	.Lnoluck
	jmp	.LzhaoxinEnd
.Lzhaoxin:
	cmp	\$`"0x".unpack("H*",'hS  ')`,%ebx
	jne	.Lnoluck
	cmp	\$`"0x".unpack("H*",'hgna')`,%edx
	jne	.Lnoluck
	cmp	\$`"0x".unpack("H*",'  ia')`,%ecx
	jne	.Lnoluck
.LzhaoxinEnd:
	mov	\$0xC0000000,%eax
	cpuid
	mov	%eax,%edx
	xor	%eax,%eax
	cmp	\$0xC0000001,%edx
	jb	.Lnoluck
	mov	\$0xC0000001,%eax
	cpuid
	mov	%edx,%eax
	and	\$0xffffffef,%eax
	or	\$0x10,%eax		# set Nano bit#4
.Lnoluck:
	mov	%r8,%rbx
	ret
.size	padlock_capability,.-padlock_capability

.globl	padlock_key_bswap
.type	padlock_key_bswap,\@abi-omnipotent,0
.align	16
padlock_key_bswap:
	mov	240($arg1),%edx
.Lbswap_loop:
	mov	($arg1),%eax
	bswap	%eax
	mov	%eax,($arg1)
	lea	4($arg1),$arg1
	sub	\$1,%edx
	jnz	.Lbswap_loop
	ret
.size	padlock_key_bswap,.-padlock_key_bswap

.globl	padlock_verify_context
.type	padlock_verify_context,\@abi-omnipotent
.align	16
padlock_verify_context:
	mov	$arg1,$ctx
	pushf
	lea	.Lpadlock_saved_context(%rip),%rax
	call	_padlock_verify_ctx
	lea	8(%rsp),%rsp
	ret
.size	padlock_verify_context,.-padlock_verify_context

.type	_padlock_verify_ctx,\@abi-omnipotent
.align	16
_padlock_verify_ctx:
	mov	8(%rsp),%r8
	bt	\$30,%r8
	jnc	.Lverified
	cmp	(%rax),$ctx
	je	.Lverified
	pushf
	popf
.Lverified:
	mov	$ctx,(%rax)
	ret
.size	_padlock_verify_ctx,.-_padlock_verify_ctx

.globl	padlock_reload_key
.type	padlock_reload_key,\@abi-omnipotent
.align	16
padlock_reload_key:
	pushf
	popf
	ret
.size	padlock_reload_key,.-padlock_reload_key

.globl	padlock_aes_block
.type	padlock_aes_block,\@function,3
.align	16
padlock_aes_block:
	mov	%rbx,%r8
	mov	\$1,$len
	lea	32($ctx),%rbx		# key
	lea	16($ctx),$ctx		# control word
	.byte	0xf3,0x0f,0xa7,0xc8	# rep xcryptecb
	mov	%r8,%rbx
	ret
.size	padlock_aes_block,.-padlock_aes_block

.globl	padlock_xstore
.type	padlock_xstore,\@function,2
.align	16
padlock_xstore:
	mov	%esi,%edx
	.byte	0x0f,0xa7,0xc0		# xstore
	ret
.size	padlock_xstore,.-padlock_xstore

.globl	padlock_sha1_oneshot
.type	padlock_sha1_oneshot,\@function,3
.align	16
padlock_sha1_oneshot:
	mov	%rdx,%rcx
	mov	%rdi,%rdx		# put aside %rdi
	movups	(%rdi),%xmm0		# copy-in context
	sub	\$128+8,%rsp
	mov	16(%rdi),%eax
	movaps	%xmm0,(%rsp)
	mov	%rsp,%rdi
	mov	%eax,16(%rsp)
	xor	%rax,%rax
	.byte	0xf3,0x0f,0xa6,0xc8	# rep xsha1
	movaps	(%rsp),%xmm0
	mov	16(%rsp),%eax
	add	\$128+8,%rsp
	movups	%xmm0,(%rdx)		# copy-out context
	mov	%eax,16(%rdx)
	ret
.size	padlock_sha1_oneshot,.-padlock_sha1_oneshot

.globl	padlock_sha1_blocks
.type	padlock_sha1_blocks,\@function,3
.align	16
padlock_sha1_blocks:
	mov	%rdx,%rcx
	mov	%rdi,%rdx		# put aside %rdi
	movups	(%rdi),%xmm0		# copy-in context
	sub	\$128+8,%rsp
	mov	16(%rdi),%eax
	movaps	%xmm0,(%rsp)
	mov	%rsp,%rdi
	mov	%eax,16(%rsp)
	mov	\$-1,%rax
	.byte	0xf3,0x0f,0xa6,0xc8	# rep xsha1
	movaps	(%rsp),%xmm0
	mov	16(%rsp),%eax
	add	\$128+8,%rsp
	movups	%xmm0,(%rdx)		# copy-out context
	mov	%eax,16(%rdx)
	ret
.size	padlock_sha1_blocks,.-padlock_sha1_blocks

.globl	padlock_sha256_oneshot
.type	padlock_sha256_oneshot,\@function,3
.align	16
padlock_sha256_oneshot:
	mov	%rdx,%rcx
	mov	%rdi,%rdx		# put aside %rdi
	movups	(%rdi),%xmm0		# copy-in context
	sub	\$128+8,%rsp
	movups	16(%rdi),%xmm1
	movaps	%xmm0,(%rsp)
	mov	%rsp,%rdi
	movaps	%xmm1,16(%rsp)
	xor	%rax,%rax
	.byte	0xf3,0x0f,0xa6,0xd0	# rep xsha256
	movaps	(%rsp),%xmm0
	movaps	16(%rsp),%xmm1
	add	\$128+8,%rsp
	movups	%xmm0,(%rdx)		# copy-out context
	movups	%xmm1,16(%rdx)
	ret
.size	padlock_sha256_oneshot,.-padlock_sha256_oneshot

.globl	padlock_sha256_blocks
.type	padlock_sha256_blocks,\@function,3
.align	16
padlock_sha256_blocks:
	mov	%rdx,%rcx
	mov	%rdi,%rdx		# put aside %rdi
	movups	(%rdi),%xmm0		# copy-in context
	sub	\$128+8,%rsp
	movups	16(%rdi),%xmm1
	movaps	%xmm0,(%rsp)
	mov	%rsp,%rdi
	movaps	%xmm1,16(%rsp)
	mov	\$-1,%rax
	.byte	0xf3,0x0f,0xa6,0xd0	# rep xsha256
	movaps	(%rsp),%xmm0
	movaps	16(%rsp),%xmm1
	add	\$128+8,%rsp
	movups	%xmm0,(%rdx)		# copy-out context
	movups	%xmm1,16(%rdx)
	ret
.size	padlock_sha256_blocks,.-padlock_sha256_blocks

.globl	padlock_sha512_blocks
.type	padlock_sha512_blocks,\@function,3
.align	16
padlock_sha512_blocks:
	mov	%rdx,%rcx
	mov	%rdi,%rdx		# put aside %rdi
	movups	(%rdi),%xmm0		# copy-in context
	sub	\$128+8,%rsp
	movups	16(%rdi),%xmm1
	movups	32(%rdi),%xmm2
	movups	48(%rdi),%xmm3
	movaps	%xmm0,(%rsp)
	mov	%rsp,%rdi
	movaps	%xmm1,16(%rsp)
	movaps	%xmm2,32(%rsp)
	movaps	%xmm3,48(%rsp)
	.byte	0xf3,0x0f,0xa6,0xe0	# rep xha512
	movaps	(%rsp),%xmm0
	movaps	16(%rsp),%xmm1
	movaps	32(%rsp),%xmm2
	movaps	48(%rsp),%xmm3
	add	\$128+8,%rsp
	movups	%xmm0,(%rdx)		# copy-out context
	movups	%xmm1,16(%rdx)
	movups	%xmm2,32(%rdx)
	movups	%xmm3,48(%rdx)
	ret
.size	padlock_sha512_blocks,.-padlock_sha512_blocks
___

sub generate_mode {
my ($mode,$opcode) = @_;
# int padlock_$mode_encrypt(void *out, const void *inp,
#		struct padlock_cipher_data *ctx, size_t len);
$code.=<<___;
.globl	padlock_${mode}_encrypt
.type	padlock_${mode}_encrypt,\@function,4
.align	16
padlock_${mode}_encrypt:
	push	%rbp
	push	%rbx

	xor	%eax,%eax
	test	\$15,$ctx
	jnz	.L${mode}_abort
	test	\$15,$len
	jnz	.L${mode}_abort
	lea	.Lpadlock_saved_context(%rip),%rax
	pushf
	cld
	call	_padlock_verify_ctx
	lea	16($ctx),$ctx		# control word
	xor	%eax,%eax
	xor	%ebx,%ebx
	testl	\$`1<<5`,($ctx)		# align bit in control word
	jnz	.L${mode}_aligned
	test	\$0x0f,$out
	setz	%al			# !out_misaligned
	test	\$0x0f,$inp
	setz	%bl			# !inp_misaligned
	test	%ebx,%eax
	jnz	.L${mode}_aligned
	neg	%rax
	mov	\$$PADLOCK_CHUNK,$chunk
	not	%rax			# out_misaligned?-1:0
	lea	(%rsp),%rbp
	cmp	$chunk,$len
	cmovc	$len,$chunk		# chunk=len>PADLOCK_CHUNK?PADLOCK_CHUNK:len
	and	$chunk,%rax		# out_misaligned?chunk:0
	mov	$len,$chunk
	neg	%rax
	and	\$$PADLOCK_CHUNK-1,$chunk	# chunk%=PADLOCK_CHUNK
	lea	(%rax,%rbp),%rsp
	mov	\$$PADLOCK_CHUNK,%rax
	cmovz	%rax,$chunk			# chunk=chunk?:PADLOCK_CHUNK
___
$code.=<<___				if ($mode eq "ctr32");
.L${mode}_reenter:
	mov	-4($ctx),%eax		# pull 32-bit counter
	bswap	%eax
	neg	%eax
	and	\$`$PADLOCK_CHUNK/16-1`,%eax
	mov	\$$PADLOCK_CHUNK,$chunk
	shl	\$4,%eax
	cmovz	$chunk,%rax
	cmp	%rax,$len
	cmova	%rax,$chunk		# don't let counter cross PADLOCK_CHUNK
	cmovbe	$len,$chunk
___
$code.=<<___				if ($PADLOCK_PREFETCH{$mode});
	cmp	$chunk,$len
	ja	.L${mode}_loop
	mov	$inp,%rax		# check if prefetch crosses page
	cmp	%rsp,%rbp
	cmove	$out,%rax
	add	$len,%rax
	neg	%rax
	and	\$0xfff,%rax		# distance to page boundary
	cmp	\$$PADLOCK_PREFETCH{$mode},%rax
	mov	\$-$PADLOCK_PREFETCH{$mode},%rax
	cmovae	$chunk,%rax		# mask=distance<prefetch?-prefetch:-1
	and	%rax,$chunk
	jz	.L${mode}_unaligned_tail
___
$code.=<<___;
	jmp	.L${mode}_loop
.align	16
.L${mode}_loop:
	cmp	$len,$chunk		# ctr32 artefact
	cmova	$len,$chunk		# ctr32 artefact
	mov	$out,%r8		# save parameters
	mov	$inp,%r9
	mov	$len,%r10
	mov	$chunk,$len
	mov	$chunk,%r11
	test	\$0x0f,$out		# out_misaligned
	cmovnz	%rsp,$out
	test	\$0x0f,$inp		# inp_misaligned
	jz	.L${mode}_inp_aligned
	shr	\$3,$len
	.byte	0xf3,0x48,0xa5		# rep movsq
	sub	$chunk,$out
	mov	$chunk,$len
	mov	$out,$inp
.L${mode}_inp_aligned:
	lea	-16($ctx),%rax		# ivp
	lea	16($ctx),%rbx		# key
	shr	\$4,$len
	.byte	0xf3,0x0f,0xa7,$opcode	# rep xcrypt*
___
$code.=<<___				if ($mode !~ /ecb|ctr/);
	movdqa	(%rax),%xmm0
	movdqa	%xmm0,-16($ctx)		# copy [or refresh] iv
___
$code.=<<___				if ($mode eq "ctr32");
	mov	-4($ctx),%eax		# pull 32-bit counter
	test	\$0xffff0000,%eax
	jnz	.L${mode}_no_carry
	bswap	%eax
	add	\$0x10000,%eax
	bswap	%eax
	mov	%eax,-4($ctx)
.L${mode}_no_carry:
___
$code.=<<___;
	mov	%r8,$out		# restore parameters
	mov	%r11,$chunk
	test	\$0x0f,$out
	jz	.L${mode}_out_aligned
	mov	$chunk,$len
	lea	(%rsp),$inp
	shr	\$3,$len
	.byte	0xf3,0x48,0xa5		# rep movsq
	sub	$chunk,$out
.L${mode}_out_aligned:
	mov	%r9,$inp
	mov	%r10,$len
	add	$chunk,$out
	add	$chunk,$inp
	sub	$chunk,$len
	mov	\$$PADLOCK_CHUNK,$chunk
___
					if (!$PADLOCK_PREFETCH{$mode}) {
$code.=<<___;
	jnz	.L${mode}_loop
___
					} else {
$code.=<<___;
	jz	.L${mode}_break
	cmp	$chunk,$len
	jae	.L${mode}_loop
___
$code.=<<___				if ($mode eq "ctr32");
	mov	$len,$chunk
	mov	$inp,%rax		# check if prefetch crosses page
	cmp	%rsp,%rbp
	cmove	$out,%rax
	add	$len,%rax
	neg	%rax
	and	\$0xfff,%rax		# distance to page boundary
	cmp	\$$PADLOCK_PREFETCH{$mode},%rax
	mov	\$-$PADLOCK_PREFETCH{$mode},%rax
	cmovae	$chunk,%rax
	and	%rax,$chunk
	jnz	.L${mode}_loop
___
$code.=<<___;
.L${mode}_unaligned_tail:
	xor	%eax,%eax
	cmp	%rsp,%rbp
	cmove	$len,%rax
	mov	$out,%r8		# save parameters
	mov	$len,$chunk
	sub	%rax,%rsp		# alloca
	shr	\$3,$len
	lea	(%rsp),$out
	.byte	0xf3,0x48,0xa5		# rep movsq
	mov	%rsp,$inp
	mov	%r8, $out		# restore parameters
	mov	$chunk,$len
	jmp	.L${mode}_loop
.align	16
.L${mode}_break:
___
					}
$code.=<<___;
	cmp	%rbp,%rsp
	je	.L${mode}_done

	pxor	%xmm0,%xmm0
	lea	(%rsp),%rax
.L${mode}_bzero:
	movaps	%xmm0,(%rax)
	lea	16(%rax),%rax
	cmp	%rax,%rbp
	ja	.L${mode}_bzero

.L${mode}_done:
	lea	(%rbp),%rsp
	jmp	.L${mode}_exit

.align	16
.L${mode}_aligned:
___
$code.=<<___				if ($mode eq "ctr32");
	mov	-4($ctx),%eax		# pull 32-bit counter
	bswap	%eax
	neg	%eax
	and	\$0xffff,%eax
	mov	\$`16*0x10000`,$chunk
	shl	\$4,%eax
	cmovz	$chunk,%rax
	cmp	%rax,$len
	cmova	%rax,$chunk		# don't let counter cross 2^16
	cmovbe	$len,$chunk
	jbe	.L${mode}_aligned_skip

.L${mode}_aligned_loop:
	mov	$len,%r10		# save parameters
	mov	$chunk,$len
	mov	$chunk,%r11

	lea	-16($ctx),%rax		# ivp
	lea	16($ctx),%rbx		# key
	shr	\$4,$len		# len/=AES_BLOCK_SIZE
	.byte	0xf3,0x0f,0xa7,$opcode	# rep xcrypt*

	mov	-4($ctx),%eax		# pull 32-bit counter
	bswap	%eax
	add	\$0x10000,%eax
	bswap	%eax
	mov	%eax,-4($ctx)

	mov	%r10,$len		# restore parameters
	sub	%r11,$len
	mov	\$`16*0x10000`,$chunk
	jz	.L${mode}_exit
	cmp	$chunk,$len
	jae	.L${mode}_aligned_loop

.L${mode}_aligned_skip:
___
$code.=<<___				if ($PADLOCK_PREFETCH{$mode});
	lea	($inp,$len),%rbp
	neg	%rbp
	and	\$0xfff,%rbp		# distance to page boundary
	xor	%eax,%eax
	cmp	\$$PADLOCK_PREFETCH{$mode},%rbp
	mov	\$$PADLOCK_PREFETCH{$mode}-1,%rbp
	cmovae	%rax,%rbp
	and	$len,%rbp		# remainder
	sub	%rbp,$len
	jz	.L${mode}_aligned_tail
___
$code.=<<___;
	lea	-16($ctx),%rax		# ivp
	lea	16($ctx),%rbx		# key
	shr	\$4,$len		# len/=AES_BLOCK_SIZE
	.byte	0xf3,0x0f,0xa7,$opcode	# rep xcrypt*
___
$code.=<<___				if ($mode !~ /ecb|ctr/);
	movdqa	(%rax),%xmm0
	movdqa	%xmm0,-16($ctx)		# copy [or refresh] iv
___
$code.=<<___				if ($PADLOCK_PREFETCH{$mode});
	test	%rbp,%rbp		# check remainder
	jz	.L${mode}_exit

.L${mode}_aligned_tail:
	mov	$out,%r8
	mov	%rbp,$chunk
	mov	%rbp,$len
	lea	(%rsp),%rbp
	sub	$len,%rsp
	shr	\$3,$len
	lea	(%rsp),$out
	.byte	0xf3,0x48,0xa5		# rep movsq
	lea	(%r8),$out
	lea	(%rsp),$inp
	mov	$chunk,$len
	jmp	.L${mode}_loop
___
$code.=<<___;
.L${mode}_exit:
	mov	\$1,%eax
	lea	8(%rsp),%rsp
.L${mode}_abort:
	pop	%rbx
	pop	%rbp
	ret
.size	padlock_${mode}_encrypt,.-padlock_${mode}_encrypt
___
}

&generate_mode("ecb",0xc8);
&generate_mode("cbc",0xd0);
&generate_mode("cfb",0xe0);
&generate_mode("ofb",0xe8);
&generate_mode("ctr32",0xd8);	# all 64-bit CPUs have working CTR...

$code.=<<___;
.asciz	"VIA Padlock x86_64 module, CRYPTOGAMS by <appro\@openssl.org>"
.align	16
.data
.align	8
.Lpadlock_saved_context:
	.quad	0
___
$code =~ s/\`([^\`]*)\`/eval($1)/gem;

print $code;

close STDOUT;
