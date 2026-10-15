.. SPDX-License-Identifier: GPL-2.0

=================
O EFI Boot Stub
=================

Nas plataformas x86 e ARM, uma zImage/bzImage do kernel pode se passar por
uma imagem PE/COFF, convencendo assim os carregadores de firmware EFI a
carregá-la como um executável EFI. O código que modifica o cabeçalho da bzImage,
juntamente com o ponto de entrada específico do EFI para o qual o carregador de
firmware salta, são conhecidos coletivamente como o "EFI boot stub", e residem
em arch/x86/boot/header.S e drivers/firmware/efi/libstub/x86-stub.c, respectivamente.
Para ARM, o stub EFI é implementado em arch/arm/boot/compressed/efi-header.S e
drivers/firmware/efi/libstub/arm32-stub.c. O código do stub EFI compartilhado
entre arquiteturas está em drivers/firmware/efi/libstub.

Para arm64, não há suporte a kernel compactado, portanto a própria Imagem se
passa por uma imagem PE/COFF e o stub EFI é vinculado diretamente ao kernel. O
stub EFI para arm64 reside em drivers/firmware/efi/libstub/arm64.c e
drivers/firmware/efi/libstub/arm64-stub.c.

Ao usar o EFI boot stub, é possível inicializar um kernel Linux sem o uso de um
carregador de boot EFI convencional, como o grub ou elilo. Como o EFI boot stub
desempenha as tarefas de um carregador de inicialização, em certo sentido ele *É*
o carregador de inicialização.

O EFI boot stub é ativado com a opção de kernel CONFIG_EFI_STUB.


Como instalar bzImage.efi
-------------------------

A bzImage localizada em arch/x86/boot/bzImage deve ser copiada para a Partição
de Sistema EFI (ESP - EFI System Partition) e renomeada com a extensão ".efi".
Sem a extensão, o carregador de firmware EFI se recusará a executá-la. Não é
possível executar bzImage.efi a partir dos sistemas de arquivos normais do Linux
porque o firmware EFI não possui suporte para eles. Para ARM, a
arch/arm/boot/zImage deve ser copiada para a partição de sistema e pode não
precisar ser renomeada. Da mesma forma, para arm64, arch/arm64/boot/Image deve
ser copiada, mas não necessariamente renomeada.


Passando parâmetros do kernel a partir do shell EFI
---------------------------------------------------

Argumentos para o kernel podem ser passados após bzImage.efi, por exemplo::

	fs0:> bzImage.efi console=ttyS0 root=/dev/sda4

A opção "initrd="
-----------------

Como a maioria dos carregadores de inicialização, o stub EFI permite que o
usuário especifique múltiplos arquivos initrd usando a opção "initrd=". Este é
o único parâmetro de linha de comando específico do stub EFI; tudo o mais é
passado para o kernel quando ele inicializa.

O caminho para o arquivo initrd deve ser um caminho absoluto a partir do início
da ESP; nomes de caminhos relativos não funcionam. Além disso, o caminho é no
estilo EFI e os elementos de diretório devem ser separados com barras invertidas
(\). Por exemplo, dada a seguinte estrutura de diretórios::

  fs0:>
	Kernels\
			bzImage.efi
			initrd-large.img

	Ramdisks\
			initrd-small.img
			initrd-medium.img

para inicializar com o arquivo initrd-large.img se o diretório de trabalho atual
for fs0:\Kernels, o seguinte comando deve ser usado::

	fs0:\Kernels> bzImage.efi initrd=\Kernels\initrd-large.img

Observe como bzImage.efi pode ser especificado com um caminho relativo. Isso ocorre
porque a imagem que estamos executando é interpretada pelo shell EFI, que entende
caminhos relativos, enquanto o restante da linha de comando é passado para bzImage.efi.

.. hint::
   Também é possível fornecer um initrd usando um protocolo UEFI específico do
   Linux no momento da inicialização. Consulte :ref:`pe-coff-entry-point` para
   obter detalhes.

A opção "dtb="
--------------

Para as arquiteturas ARM e arm64, uma árvore de dispositivos (device tree) deve ser
fornecida ao kernel. Normalmente, o firmware deve fornecer a árvore de dispositivos
através da EFI CONFIGURATION TABLE. No entanto, a opção de linha de comando "dtb="
pode ser usada para sobrescrever a árvore de dispositivos fornecida pelo firmware
ou para fornecer uma quando o firmware não puder fazê-lo.

Observe: O firmware adiciona informações de configuração em tempo de execução à
árvore de dispositivos antes de inicializar o kernel. Se dtb= for usado para
sobrescrever a árvore de dispositivos, quaisquer dados em tempo de execução
fornecidos pelo firmware serão perdidos. A opção dtb= deve ser usada apenas como
uma ferramenta de depuração ou como último recurso quando uma árvore de dispositivos
não for fornecida na EFI CONFIGURATION TABLE.

"dtb=" é processado da mesma maneira que a opção "initrd=" descrita acima.
