
$_ = join("", <>);

s/(0x[0-9a-f]{2})/sprintf("0x%.2x",ord(pack("b8",unpack("B8",chr(hex($1))))))/gei;

s/^ /	.byte /gm;
s/[,};]+$//gm;
s/^static.*//gm;

print $_;
