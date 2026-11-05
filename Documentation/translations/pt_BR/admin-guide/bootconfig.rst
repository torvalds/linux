.. SPDX-License-Identifier: GPL-2.0

====================
Configuração de boot
====================

:Autor: Masami Hiramatsu <mhiramat@kernel.org>

Visão geral
===========

A configuração de boot expande a linha de comando atual do kernel para suportar
dados adicionais de chave-valor ao inicializar o kernel de forma eficiente.
Isso permite que os administradores passem um arquivo de configuração estruturado
por chaves.

Sintaxe do arquivo de configuração
==================================

A sintaxe de configuração de boot é uma estrutura simples de chave-valor. Cada
chave consiste em palavras conectadas por pontos, e a chave e o valor são
conectados por ``=``. A string de valor deve ser terminada pelos seguintes
delimitadores descritos abaixo.

Cada palavra-chave deve conter apenas letras do alfabeto, números, traço (``-``)
ou sublinhado (``_``). E cada valor contém apenas caracteres imprimíveis ou espaços,
exceto delimitadores como ponto e vírgula (``;``), quebra de linha (``\n``),
vírgula (``,``), cerquilha (``#``) e chave de fechamento (``}``).

Se o ``=`` for seguido por espaços em branco até um desses delimitadores, a chave
será atribuída a um valor vazio.

Para arrays, os valores do array são separados por vírgula (``,``), e comentários
e quebras de linha com nova linha (``\n``) são permitidos entre os valores do array
para facilitar a legibilidade. Assim, a primeira entrada do array deve estar na
mesma linha da chave.::

  CHAVE[.PALAVRA[...]] = VALOR[, VALOR2[...]][;]

Diferente da sintaxe de linha de comando do kernel, espaços em branco (incluindo
tabs) são ignorados ao redor da vírgula e do ``=``.

Se você quiser usar esses delimitadores em um valor, pode usar aspas duplas
(``"VALOR"``) ou aspas simples (``'VALOR'``) para colocá-lo entre aspas. Observe
que você não pode escapar essas aspas.

Pode haver uma chave que não tenha valor ou tenha um valor vazio. Essas chaves
são usadas para verificar se a chave existe ou não (como um booleano).

Sintaxe de chave-valor
----------------------

A sintaxe do arquivo de configuração de boot permite que o usuário mescle chaves
com palavras parcialmente iguais usando chaves (braces). Por exemplo::

 foo.bar.baz = value1
 foo.bar.qux.quux = value2

Elas também podem ser escritas assim::

 foo.bar {
    baz = value1
    qux.quux = value2
 }

Ou de forma mais concisa, escrita da seguinte maneira::

 foo.bar { baz = value1; qux.quux = value2 }

Em ambos os estilos, as mesmas palavras-chave são mescladas automaticamente ao
serem analisadas no momento da inicialização. Assim, você pode anexar árvores ou
pares de chave-valor semelhantes.

Valores com a mesma chave
-------------------------

É proibido que dois ou mais valores ou arrays compartilhem a mesma chave.
Por exemplo::

 foo = bar, baz
 foo = qux  # !ERRO! não podemos redefinir a mesma chave

Se você quiser atualizar o valor, deve usar o operador de substituição (override)
``:=`` explicitamente. Por exemplo::

 foo = bar, baz
 foo := qux

então, ``qux`` é atribuído à chave ``foo``. Isso é útil para sobrescrever o
valor padrão adicionando bootconfigs personalizados (parciais) sem precisar
analisar o bootconfig padrão.

Se você quiser anexar o valor a uma chave existente como membro de um array,
pode usar o operador ``+=``. Por exemplo::

 foo = bar, baz
 foo += qux

Neste caso, a chave ``foo`` conterá ``bar``, ``baz`` e ``qux``.

Além disso, subchaves e um valor podem coexistir sob uma chave pai.
Por exemplo, a seguinte configuração é permitida::

 foo = value1
 foo.bar = value2
 foo := value3 # Isto atualizará o valor de foo.

Observe que, como não há sintaxe para colocar um valor puro diretamente sob uma
chave estruturada, você deve defini-lo fora das chaves. Por exemplo::

 foo {
     bar = value1
     bar {
         baz = value2
         qux = value3
     }
 }

Além disso, a ordem do nó de valor sob uma chave é fixa. Se houver um valor e
subchaves, o valor será sempre o primeiro nó filho da chave. Portanto, se o
usuário especificar subchaves primeiro, por exemplo::

 foo.bar = value1
 foo = value2

No programa (e em /proc/bootconfig), ele será exibido conforme abaixo::

 foo = value2
 foo.bar = value1

Comentários
-----------

A sintaxe de configuração aceita comentários no estilo shell-script. Os comentários
iniciados com cerquilha ("#") até a quebra de linha ("\n") serão ignorados.

::

 # linha de comentário
 foo = value # valor atribuído a foo.
 bar = 1, # 1º elemento
       2, # 2º elemento
       3  # 3º elemento

Isto é analisado como abaixo::

 foo = value
 bar = 1, 2, 3

Observe que você NÃO pode colocar um comentário ou uma quebra de linha entre o
valor e o delimitador (``,`` ou ``;``). Isso significa que a seguinte configuração
possui um erro de sintaxe::

 key = 1 # comentário
       ,2


/proc/bootconfig
================

/proc/bootconfig é uma interface de espaço de usuário da configuração de boot.
Diferente de /proc/cmdline, este arquivo exibe a lista no estilo chave-valor.
Cada par chave-valor é mostrado em cada linha no seguinte formato::

 CHAVE[.PALAVRAS...] = "[VALOR]"[,"VALOR2"...]


Inicializando o kernel com um Boot Config
=========================================

Existem duas opções para inicializar o kernel com bootconfig: anexar o bootconfig
à imagem initrd ou embuti-lo no próprio kernel.

Anexando um Boot Config ao Initrd
---------------------------------

Como o arquivo de configuração de boot é carregado com o initrd por padrão, ele
será adicionado ao final do arquivo de imagem do initrd (initramfs) com
preenchimento (padding), tamanho, checksum e palavra mágica de 12 bytes conforme
abaixo.

[initrd][bootconfig][padding][size(le32)][checksum(le32)][#BOOTCONFIG\n]

Os campos de tamanho (size) e checksum são valores de 32 bits sem sinal em little endian.

Quando a configuração de boot é adicionada à imagem do initrd, o tamanho total
do arquivo é alinhado a 4 bytes. Para preencher a lacuna, caracteres nulos
(``\0``) serão adicionados. Assim, ``size`` é o comprimento do arquivo bootconfig
+ bytes de preenchimento.

O kernel Linux decodifica a última parte da imagem do initrd na memória para
obter os dados de configuração de boot.
Por causa desse método "piggyback", não há necessidade de alterar ou atualizar o
carregador de inicialização (boot loader) e a própria imagem do kernel, desde que
o boot loader passe o tamanho correto do arquivo initrd. Se, por qualquer motivo,
o boot loader passar um tamanho maior, o kernel não conseguirá encontrar os
dados do bootconfig.

Para realizar esta operação, o kernel Linux fornece o comando ``bootconfig`` sob
tools/bootconfig, que permite ao administrador aplicar ou excluir o arquivo de
configuração na/da imagem do initrd. Você pode compilá-lo com o seguinte comando::

 # make -C tools/bootconfig

Para adicionar seu arquivo de configuração de boot à imagem do initrd, execute o
bootconfig conforme abaixo (dados antigos são removidos automaticamente se existirem)::

 # tools/bootconfig/bootconfig -a your-config /boot/initrd.img-X.Y.Z

Para remover a configuração da imagem, você pode usar a opção -d como abaixo::

 # tools/bootconfig/bootconfig -d /boot/initrd.img-X.Y.Z

Em seguida, adicione "bootconfig" na linha de comando normal do kernel para instruir
o kernel a procurar pelo bootconfig no final do arquivo initrd.
Como alternativa, compile seu kernel com a opção Kconfig ``CONFIG_BOOT_CONFIG_FORCE``
selecionada.

Embutindo um Boot Config no kernel
----------------------------------

Se você não puder usar initrd, também poderá embutir o arquivo bootconfig no
kernel através de opções do Kconfig. Nesse caso, você precisa recompilar o
kernel com as seguintes configurações::

 CONFIG_BOOT_CONFIG_EMBED=y
 CONFIG_BOOT_CONFIG_EMBED_FILE="/CAMINHO/PARA/ARQUIVO/BOOTCONFIG"

``CONFIG_BOOT_CONFIG_EMBED_FILE`` requer um caminho absoluto ou um caminho
relativo para o arquivo bootconfig a partir da árvore de código-fonte ou da
árvore de objetos. O kernel o embutirá como o bootconfig padrão.

Assim como ao anexar o bootconfig ao initrd, você precisa da opção ``bootconfig``
na linha de comando do kernel para habilitar o bootconfig embutido ou,
alternativamente, compilar seu kernel com a opção Kconfig
``CONFIG_BOOT_CONFIG_FORCE`` selecionada.

Observe que, mesmo se você definir esta opção, poderá sobrescrever o bootconfig
embutido por outro bootconfig anexado ao initrd.

Renderizando chaves kernel.* embutidas em tempo de compilação
-------------------------------------------------------------

Por padrão, o bootconfig embutido (``CONFIG_BOOT_CONFIG_EMBED=y``) é analisado
em tempo de execução, após ``parse_early_param()`` já ter sido executado. Os
tratadores de parâmetros iniciais (early parameters) (``mem=``, ``earlycon=``,
``loglevel=``, ...) não conseguem, portanto, ver valores fornecidos através da
subárvore ``kernel`` embutida.

A opção ``CONFIG_CMDLINE_FROM_BOOTCONFIG`` resolve isso renderizando a subárvore
``kernel`` de ``CONFIG_BOOT_CONFIG_EMBED_FILE`` em uma string de cmdline plana em
tempo de compilação do kernel (via ``tools/bootconfig -C``) e prefixando-a a
``boot_command_line`` durante a configuração arquitetural inicial, de modo que
as chaves fiquem visíveis para ``parse_early_param()``.

A opção requer ``CONFIG_BOOT_CONFIG_EMBED=y``, um ``CONFIG_BOOT_CONFIG_EMBED_FILE``
não vazio, ``CONFIG_CMDLINE`` vazio e uma arquitetura que selecione
``CONFIG_ARCH_SUPPORTS_CMDLINE_FROM_BOOTCONFIG``. Atualmente apenas x86 a seleciona;
em outras arquiteturas, o bootconfig embutido ainda funciona, mas apenas por meio
do analisador tardio em tempo de execução.

A mesma ativação explícita de ``bootconfig`` se aplica como em outros lugares:
as chaves renderizadas são prefixadas apenas quando ``bootconfig`` (em qualquer
forma) aparece na linha de comando do kernel, ou quando ``CONFIG_BOOT_CONFIG_FORCE``
está definido, cujo padrão é ``y`` quando ``CONFIG_BOOT_CONFIG_EMBED`` está definido.

Por exemplo, dado::

 kernel {
   loglevel = 7
   mem = 4G
 }

o kernel inicializa como se ``loglevel=7 mem=4G`` tivesse sido prefixado à linha
de comando do carregador de inicialização, com os valores visíveis para os
tratadores analisados precocemente. Valores separados por vírgula ainda são
expandidos em múltiplas entradas de linha de comando de acordo com a convenção
de array do bootconfig -- o ``kernel.earlycon = "uart8250,io,0x3f8"`` embutido
deve estar entre aspas para resultar em uma única entrada ``earlycon=``, exatamente
como no analisador em tempo de execução.

Se a string renderizada não couber em ``COMMAND_LINE_SIZE`` juntamente com a
linha de comando existente, o prefixo será ignorado e um erro será registrado,
de modo que um bootconfig embutido superdimensionado não possa travar a inicialização.

Interação com outras fontes de linha de comando e bootconfig
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Com ``CONFIG_CMDLINE_FROM_BOOTCONFIG=y``, a subárvore ``kernel`` renderizada
comporta-se como uma linha de comando em tempo de compilação (semelhante a
``CONFIG_CMDLINE``), e não como uma fonte de bootconfig. Ela é prefixada a
``boot_command_line`` em ``setup_arch()``, antes de ``parse_early_param()`` e
muito antes de o analisador em tempo de execução inspecionar um initrd. As opções
podem chegar ao kernel a partir de até quatro locais:

- Linha de comando do carregador de inicialização (bootloader): os argumentos
  que o carregador de inicialização passa. A linha de comando embutida é
  prefixada na frente deles, portanto, para parâmetros onde o último vence
  (last-one-wins), uma opção do bootloader ainda sobrescreve o valor embutido.
  Visível em /proc/cmdline.
- Linha de comando embutida (esta opção): a subárvore ``kernel`` renderizada,
  prefixada precocemente para ser vista por ``parse_early_param()``. Visível em
  /proc/cmdline.
- Bootconfig do initrd: analisado tardiamente em ``setup_boot_config()``; suas
  chaves ``kernel`` são colocadas antes de ``boot_command_line``, ou seja, antes
  da linha de comando embutida, portanto, a regra do último vencedor favorece os
  valores embutidos. Como uma fonte de bootconfig, um bootconfig do initrd ainda
  substitui o bootconfig embutido. Visível em /proc/cmdline e /proc/bootconfig.
- Bootconfig embutido (tempo de execução): analisado tardiamente, apenas quando
  nenhum bootconfig do initrd estiver presente. Visível em /proc/cmdline e
  /proc/bootconfig.

Assim, com esta opção, os valores ``kernel.*`` embutidos têm precedência sobre os
valores ``kernel.*`` de um bootconfig do initrd: para parâmetros iniciais, o initrd
ainda não foi analisado e, para parâmetros comuns, as chaves embutidas chegam mais
tarde na linha de comando. Se você precisa que um bootconfig do initrd sobrescreva
as chaves ``kernel.*`` embutidas, deixe esta opção desativada e confie no analisador
em tempo de execução.

A string renderizada faz parte da linha de comando, portanto aparece em
/proc/cmdline. Ela deliberadamente não é mostrada em /proc/bootconfig: esse
arquivo continua relatando a árvore de bootconfig analisada -- o bootconfig do
initrd se presente, caso contrário o bootconfig embutido -- independentemente de a
renderização da linha de comando em tempo de compilação estar ativada.

Parâmetros do kernel via Boot Config
====================================

Além da linha de comando do kernel, a configuração de boot pode ser usada para
passar os parâmetros do kernel. Todos os pares chave-valor sob a chave ``kernel``
serão passados diretamente para a linha de comando do kernel. Além disso, os pares
chave-valor sob ``init`` serão passados para o processo init via linha de comando.
Os parâmetros são concatenados com a string de linha de comando do kernel fornecida
pelo usuário na seguinte ordem, de modo que o parâmetro de linha de comando possa
sobrescrever os parâmetros de bootconfig (isso depende de como o subsistema lida com
parâmetros, mas em geral, o parâmetro anterior será sobrescrito pelo posterior)::

 [bootconfig params][cmdline params] -- [bootconfig init params][cmdline init params]

Aqui está um exemplo de arquivo bootconfig para parâmetros de kernel/init::

 kernel {
   root = 01234567-89ab-cdef-0123-456789abcd
 }
 init {
  splash
 }

Isto será copiado na string de linha de comando do kernel da seguinte forma::

 root="01234567-89ab-cdef-0123-456789abcd" -- splash

Se o usuário fornecer alguma outra linha de comando como::

 ro bootconfig -- quiet

A linha de comando final do kernel será a seguinte::

 root="01234567-89ab-cdef-0123-456789abcd" ro bootconfig -- splash quiet


Limitações do arquivo de configuração
=====================================

Atualmente, o tamanho máximo da configuração é de 32 KB e o total de palavras-chave
(não entradas de chave-valor) deve ser inferior a 1024 nós.
Nota: este não é o número de entradas, mas de nós; uma entrada deve consumir mais
de 2 nós (uma palavra-chave e um valor). Portanto, teoricamente, haverá até 512
pares chave-valor. Se as chaves contiverem 3 palavras em média, poderá conter 256
pares chave-valor. Na maioria dos casos, o número de itens de configuração estará
abaixo de 100 entradas e será menor que 8 KB, o que seria suficiente.
Se o número de nós exceder 1024, o analisador retornará um erro mesmo se o tamanho
do arquivo for menor que 32 KB. (Observe que esse tamanho máximo não inclui os
caracteres nulos de preenchimento.)
De qualquer forma, como o comando bootconfig verifica isso ao anexar uma configuração
de boot à imagem initrd, o usuário pode perceber antes da inicialização.


APIs do Bootconfig
==================

O usuário pode consultar ou iterar sobre pares chave-valor; também é possível
encontrar um nó de chave raiz (prefixo) e encontrar chaves-valores sob esse nó.

Se você tiver uma string de chave, poderá consultar o valor diretamente com a chave
usando xbc_find_value(). Se quiser saber quais chaves existem no bootconfig, pode
usar xbc_for_each_key_value() para iterar sobre os pares chave-valor.
Observe que você precisa usar xbc_array_for_each_value() para acessar o valor de
cada array, por exemplo::

 vnode = NULL;
 xbc_find_value("key.word", &vnode);
 if (vnode && xbc_node_is_array(vnode))
    xbc_array_for_each_value(vnode, value) {
      printk("%s ", value);
    }

Se você quiser focar em chaves que tenham uma string de prefixo, pode usar
xbc_find_node() para encontrar um nó pela string de prefixo e iterar pelas chaves
sob o nó de prefixo com xbc_node_for_each_key_value().

Mas o uso mais típico é obter o valor nomeado sob o prefixo ou obter o array
nomeado sob o prefixo, como abaixo::

 root = xbc_find_node("key.prefix");
 value = xbc_node_find_value(root, "option", &vnode);
 ...
 xbc_node_for_each_array_value(root, "array-option", value, anode) {
    ...
 }

Isso acessa um valor de "key.prefix.option" e um array de "key.prefix.array-option".

O bloqueio (locking) não é necessário, pois após a inicialização, a configuração
torna-se somente leitura. Todos os dados e chaves devem ser copiados se você
precisar modificá-los.


Funções e estruturas
====================

Para a documentação kernel-doc das definições, consulte:

 - include/linux/bootconfig.h
 - lib/bootconfig.c
