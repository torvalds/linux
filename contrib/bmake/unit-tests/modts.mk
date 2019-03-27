
LIST= one two three
LIST+= four five six

FU_mod-ts = a / b / cool

AAA= a a a
B.aaa= Baaa

all:   mod-ts

# Use print or printf iff they are builtin.
# XXX note that this causes problems, when make decides 
# there is no need to use a shell, so avoid where possible.
.if ${(type print) 2> /dev/null || echo:L:sh:Mbuiltin} != ""
PRINT= print -r --
.elif ${(type printf) 2> /dev/null || echo:L:sh:Mbuiltin} != ""
PRINT= printf '%s\n'
.else
PRINT= echo
.endif

mod-ts:
	@echo 'LIST="${LIST}"'
	@echo 'LIST:ts,="${LIST:ts,}"'
	@echo 'LIST:ts/:tu="${LIST:ts/:tu}"'
	@echo 'LIST:ts::tu="${LIST:ts::tu}"'
	@echo 'LIST:ts:tu="${LIST:ts:tu}"'
	@echo 'LIST:tu:ts/="${LIST:tu:ts/}"'
	@echo 'LIST:ts:="${LIST:ts:}"'
	@echo 'LIST:ts="${LIST:ts}"'
	@echo 'LIST:ts:S/two/2/="${LIST:ts:S/two/2/}"'
	@echo 'LIST:S/two/2/:ts="${LIST:S/two/2/:ts}"'
	@echo 'LIST:ts/:S/two/2/="${LIST:ts/:S/two/2/}"'
	@echo "Pretend the '/' in '/n' etc. below are back-slashes."
	@${PRINT} 'LIST:ts/n="${LIST:ts\n}"'
	@${PRINT} 'LIST:ts/t="${LIST:ts\t}"'
	@${PRINT} 'LIST:ts/012:tu="${LIST:ts\012:tu}"'
	@${PRINT} 'LIST:ts/xa:tu="${LIST:ts\xa:tu}"'
	@${PRINT} 'LIST:tx="${LIST:tx}"'
	@${PRINT} 'LIST:ts/x:tu="${LIST:ts\X:tu}"'
	@${PRINT} 'FU_$@="${FU_${@:ts}:ts}"'
	@${PRINT} 'FU_$@:ts:T="${FU_${@:ts}:ts:T}" == cool?'
	@${PRINT} 'B.$${AAA:ts}="${B.${AAA:ts}}" == Baaa?'
