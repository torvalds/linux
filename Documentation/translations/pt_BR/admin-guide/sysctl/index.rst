.. SPDX-License-Identifier: GPL-2.0

===========================
Documentação para /proc/sys
===========================

Copyright (c) 1998, 1999,  Rik van Riel <riel@nl.linux.org>

------------------------------------------------------------------------------

'Por que', ouço você perguntar, 'alguém iria sequer _querer_ documentação
para esses arquivos sysctl? Se alguém realmente precisa, está tudo no
código-fonte...'

Bem, esta documentação foi escrita porque algumas pessoas ou não sabem
que precisam ajustar algo, ou porque não têm tempo ou conhecimento
para ler o código-fonte.

Além disso, os programadores que construíram o sysctl o fizeram para
ser realmente utilizado, e não apenas pela diversão de programá-lo :-)

------------------------------------------------------------------------------

Aviso legal:

Como de costume, há duas coisas principais a se considerar:

1. você recebe pelo que paga
2. é de graça

As consequências são que eu não garanto a exatidão deste
documento e, se você vier reclamar comigo sobre como você bagunçou seu
sistema por causa de uma documentação incorreta, não sentirei pena de você.
Eu posso até rir de você...

Mas é claro, se você _realmente_ conseguir bagunçar seu sistema usando
apenas as opções de sysctl utilizadas neste arquivo, eu gostaria de saber
disso. Não apenas para dar boas risadas, mas também para ter certeza de que
você é a última pessoa lendo o manual (RTFM) a fazer essa besteira.

Em resumo, envie suas sugestões, correções e/ou histórias de terror
por e-mail para: <riel@nl.linux.org>

Rik van Riel.

--------------------------------------------------------------

Introdução
==========

O sysctl é um meio de configurar determinados aspectos do kernel
em tempo de execução, e o diretório /proc/sys/ existe para que você
nem precise de ferramentas especiais para fazer isso!
Na verdade, são necessárias apenas quatro coisas para usar esses recursos
de configuração:

- um sistema Linux em execução
- acesso root
- bom senso (isso é especialmente difícil de encontrar hoje em dia)
- conhecimento sobre o que todos esses valores significam

Como um rápido 'ls /proc/sys' mostrará, o diretório consiste em
vários subdiretórios (dependentes de arquitetura?). Cada subdiretório
trata principalmente de uma parte do kernel, permitindo que você faça
a configuração parte por parte, ou apenas algumas 'modificações temáticas'.

Esta documentação é sobre:

=============== ===============================================================
abi/		domínios de execução e personalidades
<$ARCH>		controles de ajuste para várias arquiteturas de CPU (ex.: csky, s390)
crypto/		subsistema criptográfico
debug/		recursos de depuração
dev/		informações específicas de dispositivos (ex.: dev/cdrom/info)
fs/		sistemas de arquivos específicos
		ajustes de manipuladores de arquivos (filehandle), inode, dentry e cotas
		binfmt_misc <Documentation/admin-guide/binfmt-misc.rst>
kernel/		informações globais / ajustes do kernel
		coisas diversas
		alguns controles específicos de arquitetura
		coisas de segurança (LSM)
net/		coisas de rede; para documentação consulte:
		<Documentation/networking/>
proc/		<vazio>
sunrpc/		SUN Remote Procedure Call (NFS)
user/		limites por namespace de usuário
vm/		ajustes de gerenciamento de memória
		gerenciamento de buffers e cache
xen/		controles do hipervisor Xen
=============== ===============================================================

Estes são os subdiretórios que tenho no meu sistema ou que foram descobertos
pesquisando pelo código-fonte. Pode haver mais ou outros subdiretórios em
outra configuração. Se você encontrar outro diretório, eu realmente gostaria de
saber sobre ele :-)

Todolist:

*   abi
*   crypto
*   debug
*   fs
*   kernel
*   net
*   sunrpc
*   user
*   vm
*   xen
