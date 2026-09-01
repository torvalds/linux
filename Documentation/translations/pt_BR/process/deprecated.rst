.. SPDX-License-Identifier: GPL-2.0

========================================================================
Interfaces, recursos de linguagem, atributos e convenções obsoletos
========================================================================

Em um mundo perfeito, seria possível converter todas as instâncias de alguma
API obsoleta para a nova API e remover completamente a API antiga em um único
ciclo de desenvolvimento. No entanto, devido ao tamanho do kernel, à hierarquia
de manutenção e ao cronograma, nem sempre é viável realizar esse tipo de
conversão de uma só vez. Isso significa que novas instâncias podem acabar
entrando no kernel enquanto as antigas estão sendo removidas, apenas aumentando
o volume de trabalho para remover a API. A fim de instruir os desenvolvedores
sobre o que se tornou obsoleto e o porquê, esta lista foi criada para servir de
referência quando o uso de elementos obsoletos for proposto para inclusão no
kernel.

__deprecated
------------
Embora este atributo marque visualmente uma interface como obsoleta, ele `não
gera mais avisos durante as compilações
<https://git.kernel.org/linus/771c035372a036f83353eef46dbb829780330234>`_ porque
um dos objetivos permanentes do kernel é compilar sem avisos (*warnings*), e
ninguém estava de fato agindo para remover essas interfaces obsoletas. Embora o
uso de `__deprecated` seja útil para sinalizar uma API antiga em um arquivo de
cabeçalho (*header file*), não é a solução completa. Tais interfaces devem ser
totalmente removidas do kernel ou adicionadas a este arquivo para desestimular
outros desenvolvedores de usá-las no futuro.

BBUG() e BUG_ON()
-----------------
Em vez disso, use WARN() e WARN_ON() e trate a condição de erro "impossível"
da forma mais amigável possível. Embora a família de APIs BUG() tenha sido
originalmente projetada para agir como uma asserção de "situação impossível" e
eliminar uma thread do kernel de forma "segura", ela se mostrou arriscada
demais. (Por exemplo: "Em que ordem os bloqueios precisam ser liberados? Os
diversos estados foram restaurados?") Muito frequentemente, o uso de BUG() vai
desestabilizar o sistema ou travá-lo por completo, o que torna impossível
depurar ou até mesmo obter relatórios de travamento (*crash reports*) viáveis.
Linus tem opiniões `muito fortes
<https://lore.kernel.org/lkml/CA+55aFy6jNLsywVYdGp83AMrXBo_P-pkjkphPGrO=82SPKCpLQ@mail.gmail.com/>`_
`sobre isso
<https://lore.kernel.org/lkml/CAHk-=whDHsbK3HTOpTF=ue_o04onRwTEaK_ZoJp_fjbqq4+=Jw@mail.gmail.com/>`_.

Note que a família WARN() só deve ser usada para situações que "espera-se que
sejam inacessíveis". Se você quiser alertar sobre situações que são
"acessíveis, mas indesejáveis", use a família de funções pr_warn(). Os
administradores do sistema podem ter configurado o sysctl *panic_on_warn* para
garantir que seus sistemas não continuem executando diante de condições
"inacessíveis". (Para exemplos, veja commits como `este aqui
<https://git.kernel.org/linus/d4689846881d160a4d12a514e991a740bcb5d65a>`_.)

Aritmética explícita em argumentos do alocador
----------------------------------------------
Cálculos dinâmicos de tamanho (especialmente multiplicação) não devem ser
realizados em argumentos de funções de alocação de memória (ou similares)
devido ao risco de estouro de capacidade (*overflow*). Isso poderia fazer com
que os valores dessem a volta (*wrap around*), resultando em uma alocação menor
do que o esperado pelo chamador. O uso dessas alocações pode levar a estouros
lineares na memória heap e a outros comportamentos incorretos. (Uma exceção a
isso são valores literais onde o compilador pode emitir um aviso se houver
risco de estouro. No entanto, a maneira preferível nesses casos é refatorar o
código conforme sugerido abaixo para evitar a aritmética explícita.)

Por exemplo, não use ``count * size`` como argumento, como em::

        foo = kmalloc(count * size, GFP_KERNEL);

Em vez disso, a forma de dois fatores do alocador deve ser utilizada::

        foo = kmalloc_array(count, size, GFP_KERNEL);

Especificamente, kmalloc() pode ser substituído por kmalloc_array(), e
kzalloc() pode ser substituído por kcalloc().

Se nenhuma forma de dois fatores estiver disponível, os auxiliares de
saturação em estouro (*saturate-on-overflow*) devem ser usados::

        bar = dma_alloc_coherent(dev, array_size(count, size), &dma, GFP_KERNEL);

Outro caso comum a ser evitado é calcular o tamanho de uma estrutura com uma
matriz final de outras estruturas, como em::

        header = kzalloc(sizeof(*header) + count * sizeof(*header->item),
                         GFP_KERNEL);

Em vez disso, use o auxiliar::

        header = kzalloc(struct_size(header, item, count), GFP_KERNEL);

.. note:: Se você estiver usando struct_size() em uma estrutura que contém uma
        matriz de comprimento zero ou de um único elemento como membro final,
        refatore o uso dessa matriz e mude para um `membro de matriz flexível
        <#zero-length-and-one-element-arrays>`_ em seu lugar.

Para outros cálculos, faça a composição usando os auxiliares size_mul(),
size_add() e size_sub(). Por exemplo, no caso de::

        foo = krealloc(current_size + chunk_size * (count - 3), GFP_KERNEL);

Em vez disso, use os auxiliares::

        foo = krealloc(size_add(current_size,
                                size_mul(chunk_size,
                                         size_sub(count, 3))), GFP_KERNEL);

Para mais detalhes, veja também array3_size() e flex_array_size(), bem como as
funções relacionadas das famílias check_mul_overflow(), check_add_overflow(),
check_sub_overflow() e check_shl_overflow().\

simple_strtol(), simple_strtoll(), simple_strtoul(), simple_strtoull()
----------------------------------------------------------------------
As funções simple_strtol(), simple_strtoll(), simple_strtoul() e
simple_strtoull() ignoram explicitamente estouros de capacidade (*overflows*),
o que pode levar a resultados inesperados nos chamadores. As respectivas
funções kstrtol(), kstrtoll(), kstrtoul() e kstrtoull() tendem a ser as
substitutas corretas, embora se deva notar que estas exigem que a string seja
terminada em NUL ou em nova linha (*newline*).

strcpy()
--------
A função strcpy() não realiza verificação de limites no buffer de destino.
Isso pode resultar em estouros lineares além do final do buffer, levando a todo
tipo de comportamentos incorretos. Embora ``CONFIG_FORTIFY_SOURCE=y`` e várias
opções do compilador ajudem a reduzir o risco de usar esta função, não há uma
boa razão para adicionar novos usos dela. A substituta segura é strscpy(),
embora se deva ter cuidado nos casos em que o valor de retorno de strcpy() era
utilizado, já que strscpy() não retorna um ponteiro para o destino, mas sim a
quantidade de bytes não-NUL copiados (ou um código de erro errno negativo quando
ocorre truncamento).

strncpy()
---------
A função strncpy() foi removida do kernel. Todos os chamadores antigos foram
migrados para alternativas mais seguras.

A função strncpy() não garantia a terminação em NUL do buffer de destino,
levando a estouros de leitura linear e outros comportamentos incorretos. Ela
também preenchia incondicionalmente o destino com NUL, o que representava uma
penalidade de desempenho desnecessária para chamadores que usavam apenas
strings terminadas em NUL. Devido aos seus diversos comportamentos, ela era uma
API ambígua para determinar qual era a real intenção do autor ao realizar a
cópia.

As substitutas para strncpy() são:

- strscpy() quando o destino deve ser terminado em NUL.
- strscpy_pad() quando o destino deve ser terminado em NUL e preenchido com
  zeros (por exemplo, estruturas que cruzam fronteiras de privilégio).
- memtostr() para destinos terminados em NUL a partir de origens de largura
  fixa não terminadas em NUL (com o atributo ``__nonstring`` na origem).
- memtostr_pad() para o mesmo caso anterior, mas com preenchimento de zeros.
- strtomem() para destinos de largura fixa não terminados em NUL, com o
  atributo ``__nonstring`` no destino.
- strtomem_pad() para destinos não terminados em NUL que também precisam de
  preenchimento com zeros.
- memcpy_and_pad() para cópias limitadas a partir de origens potencialmente não
  terminadas, onde o tamanho do destino é um valor definido em tempo de
  execução (*runtime*).

strlcpy()
---------
A função strlcpy() lê primeiro todo o buffer de origem (já que o valor de
retorno deve corresponder ao de strlen()). Essa leitura pode exceder o limite
de tamanho do destino. Isso é ineficiente e pode levar a estouros de leitura
linear se a string de origem não for terminada em NUL. A substituta segura é
strscpy(), embora se deva ter cuidado nos casos em que o valor de retorno de
strlcpy() é utilizado, já que strscpy() retornará valores negativos de errno
quando houver truncamento.

Especificador de formato %p
----------------------------
Tradicionalmente, o uso de "%p" em strings de formatação causava falhas de
exposição de endereços reais no dmesg, proc, sysfs, etc. Em vez de deixar esses
endereços expostos a explorações, todos os usos de "%p" no kernel agora são
exibidos como um valor hash, tornando-os inúteis para fins de endereçamento.
Novos usos de "%p" não devem ser adicionados ao kernel. Para endereços de texto,
usar "%pS" costuma ser melhor, pois exibe o nome do símbolo, que é muito mais
útil. Para quase todo o restante, simplesmente não adicione "%p" de forma
alguma.

Parafraseando as diretrizes atuais do Linus `guidance
<https://lore.kernel.org/lkml/CA+55aFwQEd_d40g4mUCSsVRZzrFPUJt74vc6PPpb675hYNXcKw@mail.gmail.com/>`_:

- Se o valor hash de "%p" é inútil, pergunte a si mesmo se o ponteiro em si é
  importante. Talvez ele deva ser removido por completo?
- Se você realmente acredita que o valor real do ponteiro é importante, por que
  algum estado do sistema ou nível de privilégio do usuário seria considerado
  "especial"? Se você acha que pode justificar isso (em comentários e no log de
  commit) de forma sólida o suficiente para resistir ao escrutínio do Linus,
  talvez possa usar "%px", certificando-se de aplicar permissões adequadas.

Se você estiver depurando algo em que a geração de hash de "%p" esteja causando
problemas, é possível inicializar o sistema temporariamente com a opção de
depuração "`no_hash_pointers
<https://git.kernel.org/linus/5ead723a20e0447bc7db33dc3070b420e5f80aa6>`_".

Matrizes de Tamanho Variável (VLAs)
-----------------------------------
O uso de VLAs (Variable Length Arrays) na pilha de execução gera um código de
máquina muito pior do que matrizes de tamanho estático na pilha. Embora esses
problemas significativos de `desempenho
<https://git.kernel.org/linus/02361bc77888>`_ sejam motivo suficiente para
eliminar as VLAs, elas também representam um risco de segurança. O crescimento
dinâmico de uma matriz na pilha pode exceder a memória restante no segmento da
pilha. Isso pode levar a um travamento, à possível sobrescrita de dados
sensíveis no final da pilha (quando compilado sem ``CONFIG_THREAD_INFO_IN_TASK=y``)
ou à sobrescrita de posições de memória adjacentes à pilha (quando compilado sem
``CONFIG_VMAP_STACK=y``).

Passagem direta implícita no switch case (fall-through)
-------------------------------------------------------
A linguagem C permite que o fluxo de execução de um bloco switch passe
diretamente para o próximo caso (fall-through) quando uma instrução "break"
está ausente ao final de um caso. No entanto, isso introduz ambiguidade no
código, pois nem sempre fica claro se o "break" ausente é intencional ou um bug.
Por exemplo, não é óbvio apenas olhando para o código se o ``STATE_ONE`` foi
intencionalmente projetado para passar diretamente para o ``STATE_TWO``::

        switch (value) {
        case STATE_ONE:
                do_something();
        case STATE_TWO:
                do_other();
                break;
        default:
                WARN("unknown state");
        }

Como há uma longa lista de falhas `causadas pela ausência de instruções "break"
<https://cwe.mitre.org/data/definitions/484.html>`_, não permitimos mais a
passagem direta implícita. Para identificar os casos de passagem direta
intencionais, adotamos a macro pseudo-palavra-chave "fallthrough", que se
expande para a extensão do gcc `__attribute__((__fallthrough__))
<https://gcc.gnu.org/onlinedocs/gcc/Statement-Attributes.html>`_. (Quando a
sintaxe ``[[fallthrough]]`` do C17/C18 for suportada de forma mais ampla por
compiladores C, analisadores estáticos e IDEs, poderemos mudar para o uso dessa
sintaxe para a pseudo-palavra-chave da macro.)

Todos os blocos de switch/case devem terminar com um dos seguintes elementos:

* break;
* fallthrough;
* continue;
* goto <rótulo>;
* return [expressão];

Matrizes de comprimento zero e de um único elemento
----------------------------------------------------
Há uma necessidade frequente no kernel de fornecer uma maneira de declarar uma
estrutura com um conjunto de elementos finais de tamanho dinâmico. O código do
kernel deve sempre usar `"membros de matriz flexível"
<https://en.wikipedia.org/wiki/Flexible_array_member>`_ para esses casos. O
estilo antigo de matrizes de um único elemento ou de comprimento zero não deve
mais ser utilizado.

No código C mais antigo, elementos finais de tamanho dinâmico eram declarados
especificando uma matriz de um elemento ao final de uma estrutura::

        struct something {
                size_t count;
                struct foo items[1];
        };

Isso levava a cálculos de tamanho frágeis via sizeof() (que exigiam a subtração
do tamanho do elemento final único para obter o tamanho correto do "cabeçalho").
Uma `extensão GNU C <https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html>`_ foi
introduzida para permitir matrizes de comprimento zero, a fim de evitar esses
problemas de cálculo de tamanho::

        struct something {
                size_t count;
                struct foo items[0];
        };

No entanto, isso trouxe outros problemas e não resolveu algumas limitações de
ambos os estilos, como a incapacidade de detectar quando tal matriz é usada
acidentalmente *fora* do final de uma estrutura (o que poderia ocorrer
diretamente, ou quando tal estrutura estava contida em unions, estruturas de
estruturas, etc.).

O padrão C99 introduziu os "membros de matriz flexível", nos quais a declaração
da matriz simplesmente não possui um tamanho numérico::

        struct something {
                size_t count;
                struct foo items[];
        };

Esta é a maneira como o kernel espera que elementos finais de tamanho dinâmico
sejam declarados. Isso permite que o compilador gere erros quando a matriz
flexível não for o último elemento da estrutura, o que ajuda a evitar que bugs
de `comportamento indefinido
<https://git.kernel.org/linus/76497732932f15e7323dc805e8ea8dc11bb587cf>`_ sejam
introduzidos inadvertidamente na base de código. Também permite que o
compilador analise corretamente os tamanhos das matrizes (via sizeof(),
``CONFIG_FORTIFY_SOURCE`` e ``CONFIG_UBSAN_BOUNDS``). Por exemplo, não existe um
mecanismo que nos alerte de que a seguinte aplicação do operador sizeof() a uma
matriz de comprimento zero sempre resulta em zero::

        struct something {
                size_t count;
                struct foo items[0];
        };

        struct something *instance;

        instance = kmalloc(struct_size(instance, items, count), GFP_KERNEL);
        instance->count = count;

        size = sizeof(instance->items) * instance->count;
        memcpy(instance->items, source, size);

Na última linha do código acima, ``size`` acaba sendo ``zero``, quando se
poderia pensar que ele representaria o tamanho total em bytes da memória
dinâmica recentemente alocada para a matriz final ``items``. Aqui estão alguns
exemplos deste problema: `link 1
<https://git.kernel.org/linus/f2cd32a443da694ac4e28fbf4ac6f9d5cc63a539>`_,
`link 2
<https://git.kernel.org/linus/ab91c2a89f86be2898cee208d492816ec238b2cf>`_.
Em vez disso, `membros de matriz flexível têm tipo incompleto e, portanto, o
operador sizeof() não pode ser aplicado
<https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html>`_, de modo que qualquer
uso incorreto de tais operadores será imediatamente percebido em tempo de
compilação.

Em relação às matrizes de um único elemento, é preciso estar muito ciente de que
`tais matrizes ocupam pelo menos o mesmo espaço que um único objeto daquele tipo
<https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html>`_ e, portanto, contribuem
para o tamanho da estrutura que as contém. Isso é propício a erros sempre que se
deseja calcular o tamanho total da memória dinâmica a ser alocada para uma
estrutura que contém uma matriz desse tipo como membro::

        struct something {
                size_t count;
                struct foo items[1];
        };

        struct something *instance;

        instance = kmalloc(struct_size(instance, items, count - 1), GFP_KERNEL);
        instance->count = count;

        size = sizeof(instance->items) * instance->count;
        memcpy(instance->items, source, size);

No exemplo acima, foi necessário lembrar de calcular ``count - 1`` ao usar o
auxiliar struct_size(); caso contrário, teríamos alocado memória --de forma não
intencional-- para um objeto ``items`` a mais. A maneira mais limpa e menos
sujeita a erros de implementar isso é através do uso de um `membro de matriz
flexível`, em conjunto com os auxiliares struct_size() e flex_array_size()::

        struct something {
                size_t count;
                struct foo items[];
        };

        struct something *instance;

        instance = kmalloc(struct_size(instance, items, count), GFP_KERNEL);
        instance->count = count;

        memcpy(instance->items, source, flex_array_size(instance, items, instance->count));

Existem dois casos especiais de substituição nos quais o auxiliar
DECLARE_FLEX_ARRAY() precisa ser utilizado. (Note que ele é nomeado
__DECLARE_FLEX_ARRAY() para uso em cabeçalhos de UAPI.) Esses casos ocorrem
quando a matriz flexível está sozinha em uma estrutura ou faz parte de uma
union. Isso não é permitido pela especificação C99, mas sem justificativa
técnica (como pode ser visto tanto pelo uso existente de tais matrizes nesses
locais quanto pela solução alternativa que DECLARE_FLEX_ARRAY() adota). Por
exemplo, para converter isto::

        struct something {
                ...
                union {
                        struct type1 one[0];
                        struct type2 two[0];
                };
        };

O auxiliar deve ser utilizado::

        struct something {
                ...
                union {
                        DECLARE_FLEX_ARRAY(struct type1, one);
                        DECLARE_FLEX_ARRAY(struct type2, two);
                };
        };

Atribuições diretas de kmalloc para objetos struct
--------------------------------------------------
Realizar atribuições diretas (*open-coded*) de alocações da família kmalloc()
impede que o kernel (e o compilador) consigam examinar o tipo da variável que
está recebendo a atribuição, o que limita qualquer introspecção relacionada que
possa ajudar com alinhamento, estouros de capacidade (*wrap-around*) ou
proteções adicionais (*hardening*). A família de macros kmalloc_obj() fornece
essa introspecção, que pode ser usada para os padrões de código comuns de
alocações de objetos únicos, de matrizes ou de objetos flexíveis. Por exemplo,
estas atribuições diretas::

        ptr = kmalloc(sizeof(*ptr), gfp);
        ptr = kzalloc(sizeof(*ptr), gfp);
        ptr = kmalloc_array(count, sizeof(*ptr), gfp);
        ptr = kcalloc(count, sizeof(*ptr), gfp);
        ptr = kmalloc(struct_size(ptr, flex_member, count), gfp);
        ptr = kmalloc(sizeof(struct foo), gfp);

tornam-se, respectivamente::

        ptr = kmalloc_obj(*ptr [, gfp] );
        ptr = kzalloc_obj(*ptr [, gfp] );
        ptr = kmalloc_objs(*ptr, count [, gfp] );
        ptr = kzalloc_objs(*ptr, count [, gfp] );
        ptr = kmalloc_flex(*ptr, flex_member, count [, gfp] );
        __auto_type ptr = kmalloc_obj(struct foo [, gfp] );

O argumento gfp é opcional, sendo o valor padrão GFP_KERNEL. Se
``ptr->flex_member`` estiver anotado com __counted_by(), a alocação falhará
automaticamente caso ``count`` seja maior do que o valor máximo representável
que pode ser armazenado no membro contador associado a ``flex_member``.
