.. SPDX-License-Identifier: GPL-2.0

.. raw:: latex

	\renewcommand\thesection*
	\renewcommand\thesubsection*

==================================================
Guia do usuário e do administrador do kernel Linux
==================================================

A seguir está uma coleção de documentos voltados ao usuário que foram
adicionados ao kernel ao longo do tempo. Ainda há pouca ordem ou organização
geral aqui --- este material não foi escrito para ser um documento único e
coerente! Com sorte, as coisas melhorarão rapidamente com o tempo.

Guias gerais para a administração do kernel
-------------------------------------------

Esta seção inicial contém informações gerais, incluindo o arquivo README que
descreve o kernel como um todo, documentação sobre os parâmetros do kernel,
etc.

.. toctree::
   :maxdepth: 1

   README
   devices

Todolist:

*   features

Uma grande parte da interface administrativa do kernel são os sistemas de
arquivos virtuais /proc e sysfs; estes documentos descrevem como interagir
com eles.

Todolist:

*   sysfs-rules
*   sysctl/index
*   cputopology
*   abi

Documentação relacionada à segurança:

Todolist:

*   hw-vuln/index
*   LSM/index
*   perf-security

Inicializando o kernel
----------------------

Todolist:

*   bootconfig
*   kernel-parameters
*   efi-stub
*   initrd

Rastreando e identificando problemas
------------------------------------

Aqui está um conjunto de documentos voltados a usuários que estão tentando
rastrear problemas e bugs em particular.

Todolist:

*   reporting-issues
*   reporting-regressions
*   quickly-build-trimmed-linux
*   verify-bugs-and-bisect-regressions
*   bug-hunting
*   bug-bisect
*   tainted-kernels
*   ramoops
*   dynamic-debug-howto
*   init
*   kdump/index
*   perf/index
*   pstore-blk
*   clearing-warn-once
*   kernel-per-CPU-kthreads
*   lockup-watchdogs
*   RAS/index
*   sysrq

Subsistemas centrais do kernel
------------------------------

Estes documentos descrevem interfaces de administração dos subsistemas
centrais do kernel, que provavelmente são de interesse em quase qualquer
sistema.

Todolist:

*   cgroup-v2
*   cgroup-v1/index
*   cpu-isolation
*   cpu-load
*   mm/index
*   module-signing
*   namespaces/index
*   numastat
*   pm/index
*   syscall-user-dispatch

Suporte a formatos binários não nativos. Observe que alguns destes
documentos são ... antigos ...

Todolist:

*   binfmt-misc
*   java
*   mono

Administração da camada de blocos e de sistemas de arquivos
-----------------------------------------------------------

Todolist:

*   bcache
*   binderfs
*   blockdev/index
*   cifs/index
*   device-mapper/index
*   ext4
*   filesystem-monitoring
*   nfs/index
*   iostats
*   jfs
*   md
*   ufs
*   xfs

Guias específicos de dispositivos
---------------------------------

Como configurar o seu hardware dentro do sistema Linux.

Todolist:

*   acpi/index
*   aoe/index
*   auxdisplay/index
*   braille-console
*   btmrvl
*   dell_rbu
*   edid
*   gpio/index
*   hw_random
*   laptops/index
*   lcd-panel-cgram
*   media/index
*   nvme-multipath
*   parport
*   pnp
*   rapidio
*   rtc
*   serial-console
*   svga
*   thermal/index
*   thunderbolt
*   vga-softcursor
*   video-output

Análise de carga de trabalho
----------------------------

Este é o início de uma seção com informações de interesse para
desenvolvedores de aplicações e integradores de sistemas que fazem análise
do kernel Linux para aplicações críticas de segurança. Documentos que dão
suporte à análise das interações do kernel com as aplicações, e às
expectativas dos principais subsistemas do kernel, serão encontrados aqui.

Todolist:

*   workload-tracing

Todo o resto
------------

Alguns documentos difíceis de categorizar e geralmente obsoletos.

Todolist:

*   ldm
*   unicode
