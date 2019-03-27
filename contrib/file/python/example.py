#! /usr/bin/python

import magic

ms = magic.open(magic.NONE)
ms.load()
tp = ms.file("/bin/ls")
print (tp)

f = open("/bin/ls", "rb")
buf = f.read(4096)
f.close()

tp = ms.buffer(buf)
print (tp)

ms.close()
